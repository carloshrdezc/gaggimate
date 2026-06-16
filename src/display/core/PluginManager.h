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
// logs and bails instead of recursing further.
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

  private:
    // A self-recursive event (a callback that re-triggers its own id) recurses
    // on the C stack; this caps it so a runaway cycle fails loudly instead of
    // overflowing a FreeRTOS task stack.
    static constexpr int MAX_DISPATCH_DEPTH = 16;

    using CallbackList = std::vector<EventCallback>;

    bool initialized = false;
    int dispatchDepth = 0;
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

      private:
        PluginManager *manager;
    };
};

#endif // PLUGINMANAGER_H
