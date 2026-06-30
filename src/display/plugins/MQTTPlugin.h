#ifndef MQTTPLUGIN_H
#define MQTTPLUGIN_H
#include "../core/Plugin.h"
#include <MQTT.h>
#include <WiFi.h>

constexpr int MQTT_CONNECTION_RETRIES = 5;

class MQTTPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    // Single non-blocking connect attempt (one client.connect() call, no
    // delay() loop). client.begin() is issued once per session (beginInitialized
    // guard). Returns client.connected() after the attempt. Retries are driven
    // by loop() across ticks (PRO-348), not by an in-call delay loop.
    bool connect(Controller *controller);
    // PRO-348: consumes the pending-connect latch raised by the
    // controller:wifi:connect handler and drives the non-blocking connect /
    // discovery state machine off the WiFi event task.
    void loop() override;

  private:
    void publish(const std::string &topic, const std::string &message);
    void publishBrewState(const char *state);
    void publishDiscovery(Controller *controller);
    MQTTClient client;
    WiFiClient net;

    // PRO-348 deferral state. The controller:wifi:connect handler must do no
    // blocking work on the arduino_events WiFi task, so it only latches intent
    // here (when not already connected) and returns; loop() consumes the latch.
    volatile bool wantConnect = false;      // pending connect intent latch
    volatile bool beginInitialized = false; // client.begin() issued once per session
    volatile int connectAttempts = 0;       // non-blocking attempts spent on current latch
    Controller *pendingController = nullptr;

    float lastTemperature = 0;
};

#endif // MQTTPLUGIN_H
