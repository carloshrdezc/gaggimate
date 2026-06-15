#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include "Event.h"
#include "Plugin.h"

#include <functional>
#include <map>
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
// Because of that overlap, the `listeners` map is guarded by a RECURSIVE mutex:
//   * recursive, because an event callback may itself call `trigger()` (or even
//     `on()`) re-entrantly while the current `trigger()` still holds the lock;
//   * `trigger()` copies the matching callback vector while holding the lock and
//     then invokes the copies AFTER releasing it, so a long-running or
//     re-entrant callback never blocks other tasks' registrations and never
//     iterates a vector another task is mutating.
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
    bool initialized = false;
    std::vector<Plugin *> plugins;
    std::map<std::string, std::vector<EventCallback>> listeners = {};

    // Guards `listeners` (the map AND the per-key vectors). Recursive so a
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
};

#endif // PLUGINMANAGER_H
