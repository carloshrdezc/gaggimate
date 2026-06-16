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
        // Guard against an unbounded self-triggering cycle overflowing the task
        // stack. The counter is shared across tasks (mutated only under the
        // lock, so it is race-free) rather than per-call-stack, so it bounds the
        // SUM of in-flight dispatch nesting; MAX_DISPATCH_DEPTH is set well above
        // the handful of tasks that legitimately dispatch concurrently so normal
        // fan-out never trips it, while a runaway recursion still aborts.
        if (dispatchDepth >= MAX_DISPATCH_DEPTH) {
            ESP_LOGE("PluginManager", "Dispatch depth %d exceeded for event %s; aborting to avoid stack overflow", dispatchDepth,
                     event.id.c_str());
            return;
        }
        ++dispatchDepth;
    }

    if (callbacks) {
        for (auto const &callback : *callbacks) {
            callback(event);
            if (event.stopPropagation) {
                break;
            }
        }
    }

    {
        ListenersLock lock(this);
        --dispatchDepth;
    }
}
