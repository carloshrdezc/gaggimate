#include "DiagnosticLogPlugin.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include "../core/PluginManager.h"
#include <WiFi.h>
#include <cstdarg>
#include <cstdio>
#include <esp_log.h>

static constexpr char LOG_TAG[] = "DiagnosticLogPlugin";

// A single log line, carried through the FreeRTOS queue by value so the hook
// never shares a buffer with the drain task.
struct DiagLogLine {
    char text[DiagnosticLogPlugin::LINE_BUF_SIZE];
    size_t len;
};

vprintf_like_t DiagnosticLogPlugin::previousVprintf = nullptr;
DiagnosticLogPlugin *DiagnosticLogPlugin::instance = nullptr;

void DiagnosticLogPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    instance = this;
    // Install the tee only once WiFi is up (we need a usable network stack) and
    // only when the user explicitly enabled diagnostics. When the flag is off we
    // never touch esp_log_set_vprintf(), so the hot logging path is untouched.
    pluginManager->on("controller:wifi:connect", [this](Event const &event) { start(event); });
}

void DiagnosticLogPlugin::start(Event const &event) {
    if (installed)
        return; // idempotent: a reconnect must not stack hooks/tasks
    if (!controller->getSettings().getDiagnosticLogEnabled())
        return; // gated OFF by default — zero cost on the hot path
    const int apMode = event.getInt("AP");
    if (apMode)
        return; // AP fallback: no LAN to broadcast onto

    queue = xQueueCreate(QUEUE_DEPTH, sizeof(DiagLogLine));
    if (queue == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to allocate log queue (OOM); tee not installed");
        return;
    }

    udp.begin(UDP_PORT);

    // SECURITY / CONSENT NOTE: once installed, this tee broadcasts every INFO+
    // log line in cleartext over UDP to the LAN broadcast address. No credential
    // is emitted today (relay token and WiFi password are never logged), but the
    // stream is unencrypted and visible to the whole broadcast domain. It is
    // gated OFF by default and must be enabled explicitly from Settings — enable
    // only on a trusted network, for debugging.

    // Low-priority (1) drain task pinned to core 0 (the protocol/WiFi core), so
    // the time-critical brew control loop on core 1 is never preempted by UDP I/O.
    // Stack matches the WebUI relay task (WebUIPlugin.cpp, "WebUIRelay" = 16384):
    // both share the lwip UDP/socket send path, and a logger must never crash the
    // device on stack overflow.
    BaseType_t created = xTaskCreatePinnedToCore(drainTask, "DiagLogUDP", 16384, this, 1, &taskHandle, 0);
    if (created != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to create drain task (OOM); tee not installed");
        vQueueDelete(queue);
        queue = nullptr;
        return;
    }

    // Capture the prior vprintf (UART sink) and tee through it from our hook so
    // serial-over-USB keeps working for anyone tethered.
    previousVprintf = esp_log_set_vprintf(&DiagnosticLogPlugin::teeVprintf);
    installed = true;

    ESP_LOGI(LOG_TAG, "UDP log tee active → 255.255.255.255:%u (listen: nc -ul %u)", UDP_PORT, UDP_PORT);
}

// Runs in the logging caller's task context. MUST be fast and non-blocking:
// format into a stack buffer, push to the queue (drop on full), and ALWAYS chain
// to the previous vprintf so UART output is preserved.
int DiagnosticLogPlugin::teeVprintf(const char *format, va_list args) {
    DiagnosticLogPlugin *self = instance;

    // va_list is single-pass; copy before the first consumption so the UART
    // chain below gets an untouched list.
    va_list argsCopy;
    va_copy(argsCopy, args);

    int written = 0;
    if (self != nullptr && self->queue != nullptr) {
        DiagLogLine line;
        int n = vsnprintf(line.text, sizeof(line.text), format, argsCopy);
        if (n > 0) {
            line.len = (static_cast<size_t>(n) < sizeof(line.text)) ? static_cast<size_t>(n) : sizeof(line.text) - 1;
            // Non-blocking: drop the line rather than ever stall the caller.
            xQueueSend(self->queue, &line, 0);
        }
    }
    va_end(argsCopy);

    // Preserve UART output — never silently swallow the line.
    if (previousVprintf != nullptr) {
        written = previousVprintf(format, args);
    }
    return written;
}

void DiagnosticLogPlugin::drainTask(void *arg) {
    auto *self = static_cast<DiagnosticLogPlugin *>(arg);
    DiagLogLine line;
    for (;;) {
        // Block until a line is queued — this is the ONLY place that touches the
        // network, and it runs off the logging hot path.
        if (xQueueReceive(self->queue, &line, portMAX_DELAY) == pdTRUE) {
            if (WiFi.status() != WL_CONNECTED)
                continue; // network gone; drop quietly until it returns
            IPAddress broadcast = ~WiFi.subnetMask() | WiFi.localIP();
            self->udp.beginPacket(broadcast, UDP_PORT);
            self->udp.write(reinterpret_cast<const uint8_t *>(line.text), line.len);
            self->udp.endPacket();
        }
    }
}
