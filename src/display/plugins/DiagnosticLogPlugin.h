#ifndef DIAGNOSTICLOGPLUGIN_H
#define DIAGNOSTICLOGPLUGIN_H

// PRO-266 — Diagnostic log tee.
//
// The display is permanently wired to the espresso machine, not a computer, so
// ESP_LOG output that normally goes to UART0/USB is unreachable in the field.
// This plugin installs an esp_log_set_vprintf() tee that mirrors every ESP_LOG
// line to BOTH the original UART vprintf (so a tethered USB session still works)
// AND a UDP sink on the LAN, so any computer on the same network can watch the
// device's logs live with no USB cable.
//
// Design constraints (see PR / issue):
//   * Never block inside the vprintf hook — it runs in the logging caller's task
//     context (could be the brew control or WiFi task). The hook only formats
//     into a fixed buffer and pushes the line into a FreeRTOS queue; a dedicated
//     low-priority task drains the queue and performs the actual UDP send.
//   * Gated behind Settings::getDiagnosticLogEnabled(), default OFF. When off the
//     tee is never installed and there is zero hot-path cost.
//   * Connectionless UDP broadcast (default port 9999) so it survives network
//     stalls and never blocks on a peer.

#include "../core/Plugin.h"
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct Event;
class Controller;
class PluginManager;

class DiagnosticLogPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override {}

    // UDP destination port the tee broadcasts to. Listen with e.g. `nc -ul 9999`.
    static constexpr uint16_t UDP_PORT = 9999;
    // Max characters captured per log line (longer lines are truncated).
    static constexpr size_t LINE_BUF_SIZE = 256;
    // Bounded queue depth. When full, the hook drops the line rather than block.
    static constexpr size_t QUEUE_DEPTH = 64;

  private:
    void start(Event const &event);

    // vprintf hook installed via esp_log_set_vprintf(). Static because the C API
    // takes a plain function pointer; reaches shared state through the singleton.
    static int teeVprintf(const char *format, va_list args);
    static void drainTask(void *arg);

    Controller *controller = nullptr;
    QueueHandle_t queue = nullptr;
    TaskHandle_t taskHandle = nullptr;
    WiFiUDP udp;
    bool installed = false;

    // The vprintf this plugin replaced. Called from the hook so UART output is
    // preserved. Captured from esp_log_set_vprintf()'s return value.
    static vprintf_like_t previousVprintf;
    static DiagnosticLogPlugin *instance;
};

#endif // DIAGNOSTICLOGPLUGIN_H
