#include "DiagnosticLogPlugin.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include "../core/PluginManager.h"
#include <SD_MMC.h>
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
    // PRO-271: install must be DETERMINISTIC and IDEMPOTENT, not dependent on
    // catching a single transient event. tryInstall() is the idempotent core
    // (guarded by `installed`); we drive it from three places that together
    // guarantee the tee arms whenever diagnostics is on AND WiFi is up, with no
    // reboot required:
    //   * loop()  — the backbone: re-checked every Controller iteration, so a
    //               setting flip or a late WiFi association always converges.
    //   * "controller:wifi:connect" — fast path: arm the moment STA gets an IP.
    //   * "settings:changed"        — fast path: arm the moment the user flips
    //                                 the flag on while already connected.
    // When the flag is off we never touch esp_log_set_vprintf(), so the hot
    // logging path is untouched (zero cost while default-OFF).
    pluginManager->on("controller:wifi:connect", [this](Event const &) { tryInstall(); });
    pluginManager->on("settings:changed", [this](Event const &) { tryInstall(); });
}

void DiagnosticLogPlugin::loop() { tryInstall(); }

void DiagnosticLogPlugin::tryInstall() {
    if (installed)
        return; // idempotent: a reconnect / re-check must not stack hooks/tasks
    if (!controller->getSettings().getDiagnosticLogEnabled())
        return; // gated OFF by default — zero cost on the hot path
    // Gate on the ACTUAL link state, not an event payload: we need a usable LAN
    // to broadcast onto. This also covers AP fallback (WiFi.status() is not
    // WL_CONNECTED in SoftAP mode), replacing the old, fragile event "AP" flag.
    if (WiFi.status() != WL_CONNECTED)
        return; // no STA link yet — try again next loop / next event

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

    // PRO-268: enable the persistent SD-card sink whenever a card is mounted.
    // The drain task opens the file lazily on first line; here we only record
    // intent. No card → SD sink stays off and only UDP runs (graceful no-SD).
    sdEnabled = controller->isSDCard();

    ESP_LOGI(LOG_TAG, "UDP log tee active → 255.255.255.255:%u (listen: nc -ul %u)", UDP_PORT, UDP_PORT);
    if (sdEnabled) {
        ESP_LOGI(LOG_TAG, "SD log sink active → %s (rotates to %s at %u bytes)", SD_LOG_PATH, SD_LOG_PATH_OLD,
                 static_cast<unsigned>(SD_MAX_BYTES));
    } else {
        ESP_LOGI(LOG_TAG, "SD log sink inactive (no SD card mounted)");
    }

    // PRO-271: proof-of-life. Enqueue a synthetic line DIRECTLY (not via ESP_LOG)
    // so a single UDP packet is broadcast on install regardless of whether
    // anything else is logging or how ARDUHAL routes its log levels. This makes
    // "is the tee working?" verifiable immediately on an idle machine with
    // `nc -ul 9999`. The drain task picks it up and broadcasts it like any line.
    enqueueProofOfLife();
}

// Push a one-time proof-of-life line straight into the drain queue. Same
// non-blocking contract as the hook (drop on full). Independent of ESP_LOG so
// the install is verifiable even if no other log line is ever emitted.
void DiagnosticLogPlugin::enqueueProofOfLife() {
    if (queue == nullptr)
        return;
    DiagLogLine line;
    int n =
        snprintf(line.text, sizeof(line.text), "[DiagnosticLogPlugin] proof-of-life: UDP log tee armed on %s:%u (uptime %lums)",
                 WiFi.localIP().toString().c_str(), UDP_PORT, static_cast<unsigned long>(millis()));
    if (n <= 0)
        return;
    line.len = (static_cast<size_t>(n) < sizeof(line.text)) ? static_cast<size_t>(n) : sizeof(line.text) - 1;
    xQueueSend(queue, &line, 0);
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
        // network or the SD card, and it runs off the logging hot path.
        if (xQueueReceive(self->queue, &line, portMAX_DELAY) == pdTRUE) {
            // Persistent SD sink first: this is the path that survives a WiFi/web
            // freeze or a power cycle, so it must run even when the LAN is gone.
            if (self->sdEnabled)
                self->sdAppendLine(line.text, line.len);

            if (WiFi.status() != WL_CONNECTED)
                continue; // network gone; drop UDP quietly until it returns
            IPAddress broadcast = ~WiFi.subnetMask() | WiFi.localIP();
            self->udp.beginPacket(broadcast, UDP_PORT);
            self->udp.write(reinterpret_cast<const uint8_t *>(line.text), line.len);
            self->udp.endPacket();
        }
    }
}

// Append one line to the rotating SD log. Runs only in the drain task. Every
// SD operation is best-effort: any failure disables the SD sink for the rest of
// this session so we never spin retrying a dead card, and UDP keeps working.
void DiagnosticLogPlugin::sdAppendLine(const char *text, size_t len) {
    // Lazily (re)open the active file. SD_MMC is already mounted by the
    // Controller; we reuse that same mount the display uses for the card.
    if (!sdFile) {
        if (!SD_MMC.exists(SD_LOG_DIR))
            SD_MMC.mkdir(SD_LOG_DIR);
        sdFile = SD_MMC.open(SD_LOG_PATH, FILE_APPEND);
        if (!sdFile) {
            ESP_LOGW(LOG_TAG, "SD log open failed (%s); disabling SD sink", SD_LOG_PATH);
            sdEnabled = false; // give up cleanly; UDP unaffected
            return;
        }
        sdBytes = sdFile.size();
        sdLastFlushMs = millis();
    }

    // Count only the bytes the card actually accepted. On a full/failing card
    // write() returns short (or 0); if it does, treat it as a write failure and
    // disable the SD sink for this session (same graceful-degrade as open/rotate)
    // so we never spin retrying and the byte accounting can't drift. UDP unaffected.
    const size_t wantBytes = len + 1;
    size_t wrote = sdFile.write(reinterpret_cast<const uint8_t *>(text), len);
    wrote += sdFile.write('\n');
    sdBytes += wrote;
    if (wrote < wantBytes) {
        ESP_LOGW(LOG_TAG, "SD log write short (%u/%u; card full/failing?); disabling SD sink", static_cast<unsigned>(wrote),
                 static_cast<unsigned>(wantBytes));
        sdEnabled = false;
        return;
    }
    sdSinceFlush++;

    // Flush periodically so lines actually hit the card before a freeze, but not
    // on every line (SD write amplification / card wear). Whichever of the line
    // count or the time interval trips first.
    const uint32_t now = millis();
    if (sdSinceFlush >= SD_FLUSH_EVERY_LINES || (now - sdLastFlushMs) >= SD_FLUSH_INTERVAL_MS) {
        sdFile.flush();
        sdSinceFlush = 0;
        sdLastFlushMs = now;
    }

    // Bounded growth: rotate once the active file crosses the cap.
    if (sdBytes >= SD_MAX_BYTES)
        sdRotate();
}

// Roll the active file over to the .1 slot, replacing any prior rollover, and
// start a fresh active file. Keeps the on-card footprint bounded at ~2x the cap.
void DiagnosticLogPlugin::sdRotate() {
    if (sdFile) {
        sdFile.flush();
        sdFile.close();
    }
    // Check each rename/remove: if a rotation step fails we'd otherwise keep
    // appending to an over-cap or wrong file, so degrade gracefully instead.
    // remove() of a non-existent .1 is fine on first rotate, so only the rename
    // (current → .1) and the fresh open are treated as hard failures.
    SD_MMC.remove(SD_LOG_PATH_OLD); // drop the previous rollover (absent on first rotate)
    if (!SD_MMC.rename(SD_LOG_PATH, SD_LOG_PATH_OLD)) {
        ESP_LOGW(LOG_TAG, "SD log rotate rename failed (%s → %s); disabling SD sink", SD_LOG_PATH, SD_LOG_PATH_OLD);
        sdEnabled = false;
        return;
    }
    sdFile = SD_MMC.open(SD_LOG_PATH, FILE_WRITE); // fresh, truncated active file
    if (!sdFile) {
        ESP_LOGW(LOG_TAG, "SD log rotate failed; disabling SD sink");
        sdEnabled = false;
        return;
    }
    sdBytes = 0;
    sdSinceFlush = 0;
    sdLastFlushMs = millis();
}
