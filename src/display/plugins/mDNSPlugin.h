#ifndef MDNSPLUGIN_H
#define MDNSPLUGIN_H
#include "../core/Plugin.h"

struct Event;

class mDNSPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override {};

  private:
    void start(Event const &event);

    Controller *controller = nullptr;
    // PRO-333: controller:wifi:connect can now fire more than once for a single
    // STA session (the unconditional STA_GOT_IP handler AND the end-of-setupWifi
    // trigger both fire on first connect; the watchdog re-arm fires again on
    // recovery). MDNS.begin() on an already-started responder re-allocates the
    // mDNS service and burns scarce internal RAM (PRO-334). Track started state
    // so a duplicate connect is a cheap no-op.
    bool started = false;
};

#endif // MDNSPLUGIN_H
