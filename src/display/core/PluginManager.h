#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include "Event.h"
#include "Plugin.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
#include <mutex>
#endif

using EventCallback = std::function<void(Event &)>;

// PluginManager — string-keyed event bus shared across the firmware.
//
// Threading model (CAR-110)
// -------------------------
// `on()` and `trigger()` are called from MANY FreeRTOS tasks: the controller
// loop (core 1), the WebUI/websocket relay and ShotHistory tasks (core 0), the
// NimBLE host callbacks, and the settings task. Registration is NOT confined to
// setup-time — `Controller::connect()` (which runs on the controller loop task
// at runtime, after `setup()`) registers listeners while other tasks are
// already firing events.
//
// Because of that overlap, the `listeners` map is guarded by a mutex, taken via
// the RAII `ListenersLock` guard so the lock is released on every exit path
// (including a throwing allocation) without a manual unlock to forget:
//   * `trigger()` copies the matching listener-vector handle (a cheap
//     `shared_ptr` copy — see below) while holding the lock and then invokes the
//     callbacks AFTER releasing it, so a long-running or re-entrant callback
//     never blocks other tasks' registrations and never iterates a vector
//     another task is mutating.
//   * The lock is RECURSIVE, but purely defensively. Because `trigger()`
//     releases the lock before invoking any callback, a callback that re-enters
//     `on()`/`trigger()` normally reacquires an UNCONTENDED lock — recursion is
//     not exercised by the current control flow. It is kept so the bus stays
//     deadlock-free even if a future edit ever holds the lock across a callback.
//
// Listener vectors are stored behind a `shared_ptr<const vector>` so the
// per-key callback list is COPY-ON-WRITE: `on()` builds a fresh vector and
// swaps the handle under the lock, while `trigger()` only copies the handle.
// This makes dispatch allocation-free on the hot path (the controller loop
// fires several events per sensor frame) and gives in-flight dispatches an
// immutable snapshot — a concurrent `on()` replaces the handle without touching
// the vector a dispatch is already iterating.
//
// `trigger()` dispatch is depth-limited (`MAX_DISPATCH_DEPTH`): a callback that
// re-triggers its own event id recurses on the C stack, and an unbounded cycle
// would overflow a FreeRTOS task stack (a hard reboot). Past the cap, dispatch
// logs and bails instead of recursing further. The depth counter is
// PER-CALL-STACK (per FreeRTOS task on the ESP32, `thread_local` on the host),
// not a single shared counter: each task owns its own nesting depth, so the cap
// bounds a single task's C-stack recursion and concurrent fan-out across tasks
// can never sum into the cap and silently drop a legitimate non-recursive event
// (CAR-384). A small RAII DepthGuard increments on entry and decrements on EVERY
// exit path — including a thrown callback (exceptions are enabled in the native
// test envs) — so a throwing callback can never leak depth and wedge the bus.
//
// Historical note: `trigger()` previously used `count()` followed by the `[]`
// operator, which DEFAULT-INSERTS a key on a miss — a silent write to the map
// from any task that fired an event with no registered listener. That write,
// racing against concurrent reads, is the memory corruption the old
// "register a dummy event so the event map is initialized properly" workaround
// in `setup()` was papering over. `trigger()` now uses `find()` and never
// mutates the map, so a missing-key trigger is a true read-only no-op.
class Controller;
class PluginManager {
  public:
    PluginManager();
    ~PluginManager();

    // Non-copyable and non-movable: it owns a raw SemaphoreHandle_t whose
    // ownership must not be duplicated or transferred. The deleted copy ops
    // already suppress the implicit move ops via the user-declared dtor; the
    // explicit move deletes make that intent self-documenting and survive a
    // future dtor removal.
    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;
    PluginManager(PluginManager &&) = delete;
    PluginManager &operator=(PluginManager &&) = delete;

    void registerPlugin(Plugin *plugin);

    void setup(Controller *controller);
    void loop();

    void on(const String &eventId, const EventCallback &callback);

    Event trigger(const String &eventId);
    Event trigger(const String &eventId, const String &key, const String &value);
    Event trigger(const String &eventId, const String &key, int value);
    Event trigger(const String &eventId, const String &key, float value);
    void trigger(Event &event);

    // Maximum per-call-stack dispatch recursion before trigger() refuses to
    // descend further (CAR-384). Exposed read-only so host tests assert against
    // the real cap rather than a hard-coded literal that can silently drift.
    static constexpr int maxDispatchDepth() { return MAX_DISPATCH_DEPTH; }

  private:
    // A self-recursive event (a callback that re-triggers its own id) recurses
    // on the C stack; this caps it so a runaway cycle fails loudly instead of
    // overflowing a FreeRTOS task stack. The cap applies PER CALL STACK (per
    // task), not to the sum across tasks — see enterDispatch()/DepthGuard.
    static constexpr int MAX_DISPATCH_DEPTH = 16;

    using CallbackList = std::vector<EventCallback>;

    bool initialized = false;
    std::vector<Plugin *> plugins;
    // Per-key listener lists are held by shared_ptr<const> so on() can swap in a
    // new list (copy-on-write) while a concurrent trigger() iterates the old one.
    std::map<std::string, std::shared_ptr<const CallbackList>> listeners = {};

    // Guards `listeners` (the map AND the per-key handles). Recursive so a
    // callback invoked from trigger() may re-enter on()/trigger(). See the
    // threading-model comment above for the lock discipline.
#if defined(ESP_PLATFORM)
    SemaphoreHandle_t listenersMutex = nullptr;

    void lockListeners();
    void unlockListeners();
#else
    std::recursive_mutex listenersMutex;

    void lockListeners() { listenersMutex.lock(); }
    void unlockListeners() { listenersMutex.unlock(); }
#endif

    // RAII guard for the listeners lock: takes in the ctor, gives in the dtor,
    // so every exit path (early return, thrown bad_alloc on a copy) releases the
    // lock without a manual unlock to forget.
    class ListenersLock {
      public:
        explicit ListenersLock(PluginManager *manager) : manager(manager) { manager->lockListeners(); }
        ~ListenersLock() { manager->unlockListeners(); }
        ListenersLock(const ListenersLock &) = delete;
        ListenersLock &operator=(const ListenersLock &) = delete;
        ListenersLock(ListenersLock &&) = delete;
        ListenersLock &operator=(ListenersLock &&) = delete;

      private:
        PluginManager *manager;
    };

    // Per-call-stack dispatch depth (CAR-384). On the ESP32 each FreeRTOS task
    // has its own C stack, so the recursion the cap protects is per-task; a
    // single shared counter would instead bound the SUM of in-flight dispatch
    // across all tasks and could drop a legitimate non-recursive event under
    // concurrent fan-out. We therefore key the depth by the current task handle
    // (guarded by the existing listeners lock), and use a `thread_local` counter
    // on the host. enterDispatch() returns the depth AFTER incrementing (so the
    // caller can compare against MAX_DISPATCH_DEPTH and bail); exitDispatch()
    // decrements. DepthGuard pairs them so the decrement runs on EVERY exit path,
    // including a thrown callback.
#if defined(ESP_PLATFORM)
    std::map<TaskHandle_t, int> dispatchDepthByTask = {};
#endif
    int enterDispatch();
    void exitDispatch();

    // RAII guard for the per-call-stack dispatch depth: increments on the way in
    // and decrements in the dtor, so a callback that throws (exceptions are
    // enabled in the native test envs) can never leak depth and permanently wedge
    // the bus at MAX_DISPATCH_DEPTH. `depth()` is the post-increment depth used
    // for the cap check.
    class DepthGuard {
      public:
        explicit DepthGuard(PluginManager *manager) : manager(manager), currentDepth(manager->enterDispatch()) {}
        ~DepthGuard() { manager->exitDispatch(); }
        DepthGuard(const DepthGuard &) = delete;
        DepthGuard &operator=(const DepthGuard &) = delete;
        DepthGuard(DepthGuard &&) = delete;
        DepthGuard &operator=(DepthGuard &&) = delete;
        int depth() const { return currentDepth; }

      private:
        PluginManager *manager;
        int currentDepth;
    };
};

#endif // PLUGINMANAGER_H
