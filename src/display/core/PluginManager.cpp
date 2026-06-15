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
    lockListeners();
    listeners[std::string(eventId.c_str())].push_back(callback);
    unlockListeners();
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

    // Copy the matching callbacks under the lock, then invoke them after
    // releasing it. This keeps the critical section short (no user code runs
    // while the map is locked), avoids iterating a vector another task could be
    // mutating via on(), and lets a callback re-enter on()/trigger() without
    // contending on a held lock. Use find() — never operator[] — so a trigger
    // with no registered listener stays a read-only no-op (CAR-110).
    std::vector<EventCallback> callbacks;
    lockListeners();
    auto it = listeners.find(std::string(event.id.c_str()));
    if (it != listeners.end()) {
        callbacks = it->second;
    }
    unlockListeners();

    for (auto const &callback : callbacks) {
        callback(event);
        if (event.stopPropagation) {
            break;
        }
    }
}
