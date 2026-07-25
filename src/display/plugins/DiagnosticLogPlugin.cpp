#include "DiagnosticLogPlugin.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include "../core/EventIds.h"
#include "../core/GmHeapDiag.h" // PRO-566
#include "../core/PluginManager.h"
#include "DiagLogFormat.h"
#include <SD_MMC.h>
#include <WiFi.h>
#include <cstdarg>
#include <cstdio>
#include <esp_heap_caps.h>
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

namespace {
// previousVprintf is a vprintf_like_t (takes a va_list, not varargs). To echo an
// already-formatted line we must NOT pass line.text as the format string (its
// content may contain '%'); instead format the fixed "%s" against it. This tiny
// varargs shim builds the va_list and hands it to previousVprintf, reproducing
// the caller's line verbatim on UART. Runs only in drainTask (PRO-367).
int echoToUart(vprintf_like_t sink, const char *text) {
    if (sink == nullptr || text == nullptr)
        return 0;
    // Local varargs -> va_list bridge for the vprintf_like_t UART sink.
    struct Bridge {
        static int call(vprintf_like_t s, const char *fmt, ...) {
            va_list ap;
            va_start(ap, fmt);
            int r = s(fmt, ap);
            va_end(ap);
            return r;
        }
    };
    return Bridge::call(sink, "%s", text);
}
} // namespace

// RAII guard for the install mutex. Takes the mutex on construction (if it
// exists) and gives it back on destruction, so every return path out of
// tryInstall() — the early guards, the OOM bailouts, and the normal completion —
// releases the lock without a hand-audited give at each site (mirrors the
// CAR-259 RelayLifecycleLock pattern).
namespace {
struct InstallLock {
    SemaphoreHandle_t handle;
    explicit InstallLock(SemaphoreHandle_t h) : handle(h) {
        if (handle != nullptr) {
            xSemaphoreTake(handle, portMAX_DELAY);
        }
    }
    ~InstallLock() {
        if (handle != nullptr) {
            xSemaphoreGive(handle);
        }
    }
    InstallLock(const InstallLock &) = delete;
    InstallLock &operator=(const InstallLock &) = delete;
};
} // namespace

void DiagnosticLogPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    instance = this;
    // Create the install mutex once here, before any WiFi/server events can fire,
    // so the check-and-install in tryInstall() is serialized across the three
    // tasks that can reach it (loop(), AsyncTCP EventIds::SETTINGS_CHANGED, arduino_events
    // EventIds::CONTROLLER_WIFI_CONNECT). See tryInstall() (PRO-271; CAR-259 precedent).
    if (installMutex == nullptr) {
        installMutex = xSemaphoreCreateMutex();
    }
    // PRO-271: install must be DETERMINISTIC and IDEMPOTENT, not dependent on
    // catching a single transient event. tryInstall() is the idempotent core
    // (guarded by `installed`); we drive it from three places that together
    // guarantee the tee arms whenever diagnostics is on AND WiFi is up, with no
    // reboot required:
    //   * loop()  — the backbone: re-checked every Controller iteration, so a
    //               setting flip or a late WiFi association always converges.
    //   * EventIds::CONTROLLER_WIFI_CONNECT — fast path: arm the moment STA gets an IP.
    //   * EventIds::SETTINGS_CHANGED        — fast path: arm the moment the user flips
    //                                 the flag on while already connected.
    // When the flag is off we never touch esp_log_set_vprintf(), so the hot
    // logging path is untouched (zero cost while default-OFF).
    pluginManager->on(EventIds::CONTROLLER_WIFI_CONNECT, [this](Event const &) { tryInstall(); });
    pluginManager->on(EventIds::SETTINGS_CHANGED, [this](Event const &) { tryInstall(); });
}

void DiagnosticLogPlugin::loop() { tryInstall(); }

void DiagnosticLogPlugin::tryInstall() {
    // Serialize the check-and-install. tryInstall() is reachable concurrently
    // from three FreeRTOS tasks (loop(), AsyncTCP EventIds::SETTINGS_CHANGED,
    // arduino_events EventIds::CONTROLLER_WIFI_CONNECT); without this lock two of them
    // could both pass the `installed` guard and run the irreversible install
    // twice — leaking a 16 KiB drain task + queue and capturing our own
    // teeVprintf as previousVprintf (self-recursion / stack overflow). The lock
    // makes the check-and-claim of `installed` atomic so only one task ever
    // performs the side effects (CAR-259 relayLifecycleMutex precedent).
    InstallLock lock(installMutex);
    if (installed)
        return; // idempotent: a reconnect / re-check must not stack hooks/tasks
    if (!controller->getSettings().getDiagnosticLogEnabled())
        return; // gated OFF by default — zero cost on the hot path
    // Gate on the ACTUAL link state, not an event payload: we need a usable LAN
    // to broadcast onto. This also covers AP fallback (WiFi.status() is not
    // WL_CONNECTED in SoftAP mode), replacing the old, fragile event "AP" flag.
    if (WiFi.status() != WL_CONNECTED)
        return; // no STA link yet — try again next loop / next event

    GM_HEAP_DIAG("before DiagLog install (queue+task)"); // PRO-566
    // PRO-568: place the ~16.6 KiB log queue in external PSRAM (MALLOC_CAP_SPIRAM)
    // via FreeRTOS static creation (xQueueCreateStatic), supplying both the
    // item-storage array and the control block from PSRAM. The buffers are member
    // fields owned for the plugin's lifetime; the OOM rollback below frees them if
    // task creation fails. For the full PSRAM/stack-placement rationale (why the
    // queue may live in non-DMA PSRAM but the drain-task stack cannot), see the
    // class-doc comment in DiagnosticLogPlugin.h.
    queueStorage = static_cast<uint8_t *>(heap_caps_malloc(QUEUE_DEPTH * sizeof(DiagLogLine), MALLOC_CAP_SPIRAM));
    queueControlBlock = static_cast<StaticQueue_t *>(heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_SPIRAM));
    if (queueStorage == nullptr || queueControlBlock == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to allocate log queue buffers (PSRAM OOM); tee not installed");
        freeQueueBuffers();
        return;
    }
    queue = xQueueCreateStatic(QUEUE_DEPTH, sizeof(DiagLogLine), queueStorage, queueControlBlock);
    if (queue == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to create log queue; tee not installed");
        freeQueueBuffers();
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
    // PRO-568: this 16 KiB stack stays in internal DRAM (it cannot be routed to
    // PSRAM on this platform) — see the placement rationale in DiagnosticLogPlugin.h.
    BaseType_t created = xTaskCreatePinnedToCore(drainTask, "DiagLogUDP", 16384, this, 1, &taskHandle, 0);
    if (created != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to create drain task (OOM); tee not installed");
        // Static queue: vQueueDelete unregisters it but does NOT free our
        // caller-supplied PSRAM buffers, so free those explicitly (PRO-568).
        vQueueDelete(queue);
        queue = nullptr;
        freeQueueBuffers();
        return;
    }

    // Capture the prior vprintf (UART sink) and tee through it from our hook so
    // serial-over-USB keeps working for anyone tethered.
    previousVprintf = esp_log_set_vprintf(&DiagnosticLogPlugin::teeVprintf);
    installed = true;
    GM_HEAP_DIAG("after DiagLog install (queue+task)"); // PRO-566

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

// Free the caller-supplied PSRAM buffers backing the static queue (PRO-568).
// Called only on the install OOM/rollback paths — the queue itself is never
// torn down once armed (the plugin is a long-lived singleton), so this only
// runs before the tee is fully installed. Idempotent (nulls after freeing).
void DiagnosticLogPlugin::freeQueueBuffers() {
    if (queueStorage != nullptr) {
        heap_caps_free(queueStorage);
        queueStorage = nullptr;
    }
    if (queueControlBlock != nullptr) {
        heap_caps_free(queueControlBlock);
        queueControlBlock = nullptr;
    }
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
    // No truncation clamp here (unlike teeVprintf): the proof-of-life message is
    // a fixed string plus an IPv4 address, a port, and uptime, so `n` can never
    // reach LINE_BUF_SIZE (256) — the branch would be dead. snprintf still
    // NUL-terminates within the buffer regardless.
    line.len = static_cast<size_t>(n);
    xQueueSend(queue, &line, 0);
}

// Runs in the logging caller's task context (often core-1: the brew control /
// WiFi tasks). MUST be fast and non-blocking: format into a stack buffer and push
// to the queue (drop on full). PRO-367: the UART echo (previousVprintf, a
// SYNCHRONOUS blocking ets_printf transmit) was the dominant per-line hot-path
// cost and lengthened core-1 critical sections enough to trip the 10 ms
// processMutex takes on the volumetric stop path. It is now performed by
// drainTask off the caller's task, so teeVprintf does only format+enqueue here.
int DiagnosticLogPlugin::teeVprintf(const char *format, va_list args) {
    DiagnosticLogPlugin *self = instance;

    if (self != nullptr && self->queue != nullptr) {
        DiagLogLine line;
        // Pure, host-testable format+truncation kernel (PRO-273; DiagLogFormat.h).
        line.len = diaglog::formatLine(line.text, sizeof(line.text), format, args);
        if (line.len > 0) {
            // Non-blocking: drop the line rather than ever stall the caller.
            // drainTask echoes line.text to UART + broadcasts UDP + appends SD.
            // PRO-368: on a full 64-deep queue this drops the line from ALL
            // sinks, INCLUDING the UART / serial-over-USB echo — because PRO-367
            // moved that echo into drainTask, so it is now downstream of this
            // enqueue rather than emitted here. PR #357's "serial output
            // preserved when diag ON" guarantee is therefore bounded by queue
            // capacity: under saturation the UART sink is dropped too. This is a
            // deliberate, accepted trade-off, not a defect — reintroducing a
            // synchronous UART echo on the caller is exactly the core-1
            // processMutex critical-section lengthening PRO-367 fixed, so the
            // producer path must stay non-blocking (drop-all-sinks-on-overflow).
            xQueueSend(self->queue, &line, 0);
        }
    }

    // The actual UART transmit is deferred to drainTask (PRO-367); callers ignore
    // vprintf's return, so report 0 written on the caller's task.
    return 0;
}

void DiagnosticLogPlugin::drainTask(void *arg) {
    auto *self = static_cast<DiagnosticLogPlugin *>(arg);
    DiagLogLine line;
    for (;;) {
        // Block until a line is queued — this task now owns ALL three sinks:
        // UART echo (PRO-367, moved off the core-1 caller), UDP broadcast, and
        // the SD append. Running off the logging hot path is the whole point.
        if (xQueueReceive(self->queue, &line, portMAX_DELAY) == pdTRUE) {
            // UART echo FIRST so serial-over-USB observability is preserved (and
            // is the sink most likely to survive a WiFi/SD freeze). line.text is
            // the already-formatted, NUL-terminated line the caller emitted
            // (including its trailing newline); "%s" reproduces it verbatim.
            // PRO-367 tradeoff: the blocking UART transmit no longer runs on the
            // core-1 logging caller, so a crash on core-1 mid-line may lose that
            // last in-flight line from serial (it is still enqueued for UDP/SD).
            // In exchange, ESP_LOG* on core-1 during a shot no longer lengthens
            // the critical sections that were tripping the 10 ms volumetric take.
            // (Separately, PRO-368: a full-queue enqueue in teeVprintf drops the
            // line from this UART echo too — see the saturation-drop note there.)
            if (previousVprintf != nullptr)
                echoToUart(previousVprintf, line.text);

            // Persistent SD sink: this is the path that survives a WiFi/web
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
