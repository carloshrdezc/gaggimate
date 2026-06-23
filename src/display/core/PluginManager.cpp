#include "PluginManager.h"

#if defined(ESP_PLATFORM)
PluginManager::PluginManager() {
    listenersMutex = xSemaphoreCreateRecursiveMutex();
    // A null handle would make lock/unlock no-ops and silently run the event
    // bus unsynchronized — reintroducing the exact CAR-110 race. Fail loudly
    // instead: this ctor runs once early in boot when heap is plentiful, so a
    // failure here is a genuine fault, not a condition to tolerate.
    if (listenersMutex == nullptr) {
        ESP_LOGE("PluginManager", "Failed to create listeners mutex");
        configASSERT(listenersMutex != nullptr);
    }
}

PluginManager::~PluginManager() {
    if (listenersMutex != nullptr) {
        vSemaphoreDelete(listenersMutex);
        listenersMutex = nullptr;
    }
}

void PluginManager::lockListeners() {
    if (listenersMutex != nullptr) {
        xSemaphoreTakeRecursive(listenersMutex, portMAX_DELAY);
    }
}

void PluginManager::unlockListeners() {
    if (listenersMutex != nullptr) {
        xSemaphoreGiveRecursive(listenersMutex);
    }
}
#else
PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;
#endif

// enterDispatch/exitDispatch maintain the PER-CALL-STACK dispatch depth used to
// cap runaway self-recursion (CAR-384). On the ESP32 the "call stack" is a
// FreeRTOS task, so the depth is keyed by the current task handle under the
// existing listeners lock (the only place dispatchDepthByTask is touched). On
// the host each thread gets its own depth via thread_local. Either way the cap
// bounds one task's recursion, never the sum of concurrent dispatches across
// tasks.
#if defined(ESP_PLATFORM)
int PluginManager::enterDispatch() {
    ListenersLock lock(this);
    int depth = ++dispatchDepthByTask[xTaskGetCurrentTaskHandle()];
    return depth;
}

void PluginManager::exitDispatch() {
    ListenersLock lock(this);
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    auto it = dispatchDepthByTask.find(task);
    if (it != dispatchDepthByTask.end()) {
        if (--it->second <= 0) {
            // Drop the entry at depth 0 so the map does not grow one slot per
            // task that ever dispatches and never shrink.
            dispatchDepthByTask.erase(it);
        }
    }
}
#else
namespace {
// Single per-thread (per-call-stack) dispatch depth shared by enter/exit on the
// host. Must be ONE object — two separate function-local statics would not share
// state and the depth would never balance.
int &hostDispatchDepth() {
    static thread_local int depth = 0;
    return depth;
}
} // namespace

int PluginManager::enterDispatch() { return ++hostDispatchDepth(); }

void PluginManager::exitDispatch() { --hostDispatchDepth(); }
#endif

void PluginManager::registerPlugin(Plugin *plugin) { plugins.push_back(plugin); }

void PluginManager::setup(Controller *controller) {
    ESP_LOGV("PluginManager", "Setting up PluginManager");
    for (const auto &plugin : plugins) {
        plugin->setup(controller, this);
    }
    initialized = true;
}

void PluginManager::loop() {
    if (!initialized)
        return;
    for (auto &plugin : plugins) {
        plugin->loop();
    }
}

void PluginManager::on(const String &eventId, const EventCallback &callback) {
    ESP_LOGV("PluginManager", "Registering listener: %s", eventId.c_str());
    const std::string key(eventId.c_str());
    ListenersLock lock(this);
    // Copy-on-write: build a fresh list from the existing one (if any) and swap
    // in a new handle. A concurrent trigger() holding the old shared_ptr keeps
    // iterating an immutable snapshot — its vector is never mutated underneath it.
    auto it = listeners.find(key);
    auto next = std::make_shared<CallbackList>(it != listeners.end() ? *it->second : CallbackList{});
    next->push_back(callback);
    listeners[key] = std::move(next);
}

Event PluginManager::trigger(const String &eventId) {
    Event event;
    event.id = eventId;
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const String &value) {
    Event event;
    event.id = eventId;
    event.setString(key, value);
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const int value) {
    Event event;
    event.id = eventId;
    event.setInt(key, value);
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const float value) {
    Event event;
    event.id = eventId;
    event.setFloat(key, value);
    trigger(event);
    return event;
}

void PluginManager::trigger(Event &event) {
    ESP_LOGV("PluginManager", "Triggering event: %s", event.id.c_str());

    // Grab the matching listener-list handle under the lock, then invoke the
    // callbacks after releasing it. Copying the shared_ptr (not the vector)
    // keeps the critical section short and allocation-free, runs no user code
    // while the map is locked, and lets a callback re-enter on()/trigger()
    // without contending on a held lock. Use find() — never operator[] — so a
    // trigger with no registered listener stays a read-only no-op (CAR-110).
    std::shared_ptr<const CallbackList> callbacks;
    {
        ListenersLock lock(this);
        auto it = listeners.find(std::string(event.id.c_str()));
        if (it != listeners.end()) {
            callbacks = it->second;
        }
    }

    // Guard against an unbounded self-triggering cycle overflowing the task
    // stack. The depth is PER CALL STACK (per FreeRTOS task on the ESP32,
    // thread_local on the host), so the cap bounds a single task's C-stack
    // recursion — concurrent fan-out across tasks can never sum into the cap and
    // drop a legitimate non-recursive event (CAR-384). DepthGuard increments on
    // construction and decrements in its dtor on EVERY exit path, so a throwing
    // callback can't leak depth and permanently wedge the bus at the cap.
    DepthGuard guard(this);
    if (guard.depth() > MAX_DISPATCH_DEPTH) {
        ESP_LOGE("PluginManager", "Dispatch depth %d exceeded for event %s; aborting to avoid stack overflow", guard.depth(),
                 event.id.c_str());
        return;
    }

    if (callbacks) {
        for (auto const &callback : *callbacks) {
            callback(event);
            if (event.stopPropagation) {
                break;
            }
        }
    }
}
