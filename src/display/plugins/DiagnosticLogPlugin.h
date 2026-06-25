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
// PRO-268 (slice 2): the UDP stream goes silent if the WiFi/web stack itself
// freezes (the PRO-262 failure mode) and a hard power cycle loses RAM. So the
// same drain task ALSO appends every line to a rotating file on the SD card
// (when one is mounted), giving persistent, post-reboot-readable capture of the
// last lines. The SD write happens ONLY in the drain task — never in the
// vprintf hook, which stays a non-blocking queue push + drop-on-full.
//
// Design constraints (see PR / issue):
//   * Never block inside the vprintf hook — it runs in the logging caller's task
//     context (could be the brew control or WiFi task). The hook only formats
//     into a fixed buffer and pushes the line into a FreeRTOS queue; a dedicated
//     low-priority task drains the queue and performs the actual UDP send AND
//     the SD-card append.
//   * Gated behind Settings::getDiagnosticLogEnabled(), default OFF. When off the
//     tee is never installed and there is zero hot-path cost. The SD sink shares
//     this gate: it is active whenever diagnostics are on AND an SD card is
//     mounted (Controller::isSDCard()); no separate flag.
//   * Install is DETERMINISTIC + IDEMPOTENT (PRO-271): tryInstall() arms the tee
//     whenever the flag is on AND WiFi (STA) is connected, and is driven from
//     loop() every iteration plus the "controller:wifi:connect" / "settings:changed"
//     events. So enabling diagnostics while already online arms it with NO reboot,
//     and the install no longer hinges on catching one transient WiFi event. The
//     `installed` flag makes every re-attempt a no-op. On install a one-time
//     proof-of-life line is broadcast straight into the drain queue (independent
//     of ESP_LOG) so the feature is verifiable immediately on an idle machine.
//   * Connectionless UDP broadcast (default port 9999) so it survives network
//     stalls and never blocks on a peer.
//   * SD append is best-effort: if no card is mounted, or a write/open fails, the
//     SD sink is skipped cleanly and the UDP path is unaffected (never crashes).
//   * SD contention: the SD sink shares the single SD_MMC volume with ShotHistory,
//     BeanManager, etc. If diagnostics is enabled mid-shot, the drain task's SD
//     appends and ShotHistory's SD writes can hit the mount concurrently. This is
//     SAFE — the build sets FF_FS_REENTRANT=1, so FatFS serializes all access to a
//     volume behind a per-volume mutex (no corruption); the only cost is bounded
//     contention/latency. And because the feature is default-OFF, there is no
//     impact at all unless a user explicitly enables diagnostics mid-shot.

#include "../core/Plugin.h"
#include <FS.h>
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
    void loop() override;

    // UDP destination port the tee broadcasts to. Listen with e.g. `nc -ul 9999`.
    static constexpr uint16_t UDP_PORT = 9999;
    // Max characters captured per log line (longer lines are truncated).
    static constexpr size_t LINE_BUF_SIZE = 256;
    // Bounded queue depth. When full, the hook drops the line rather than block.
    static constexpr size_t QUEUE_DEPTH = 64;

    // --- SD-card rotating sink (PRO-268) ---
    // Directory + active/rotated file names on the SD card. The drain task
    // appends each line to SD_LOG_PATH; once it grows past SD_MAX_BYTES it is
    // renamed to SD_LOG_PATH_OLD (replacing any previous rollover) and a fresh
    // active file is started. Total on-card footprint is therefore bounded at
    // ~2 * SD_MAX_BYTES.
    static constexpr char SD_LOG_DIR[] = "/diag";
    static constexpr char SD_LOG_PATH[] = "/diag/log.txt";
    static constexpr char SD_LOG_PATH_OLD[] = "/diag/log.1";
    // Roll the active file over at this size (256 KiB). Small enough to stay
    // cheap to read back after a reboot, large enough to hold plenty of context.
    static constexpr size_t SD_MAX_BYTES = 256UL * 1024UL;
    // Durability vs SD write amplification: flush the file handle to the card
    // after this many appended lines (or after SD_FLUSH_INTERVAL_MS, whichever
    // comes first). Without periodic flush a freeze could lose buffered lines;
    // flushing every single line would hammer the card.
    static constexpr size_t SD_FLUSH_EVERY_LINES = 16;
    static constexpr uint32_t SD_FLUSH_INTERVAL_MS = 1000;

  private:
    // Idempotent install attempt: arms the tee iff diagnostics is enabled AND
    // WiFi (STA) is connected. Guarded by `installed` so it is safe to call
    // repeatedly — from loop(), on WiFi connect, and on a settings flip — which
    // is what makes the install deterministic and reboot-free (PRO-271).
    void tryInstall();

    // Broadcast a one-time proof-of-life line on install, straight into the
    // drain queue (independent of ESP_LOG) so the feature is verifiable even on
    // an idle machine. PRO-271.
    void enqueueProofOfLife();

    // vprintf hook installed via esp_log_set_vprintf(). Static because the C API
    // takes a plain function pointer; reaches shared state through the singleton.
    static int teeVprintf(const char *format, va_list args);
    static void drainTask(void *arg);

    // SD-card sink helpers (PRO-268), all called only from the drain task:
    //   sdAppendLine() opens the active file lazily, appends one line, rotates
    //   when it crosses SD_MAX_BYTES, and flushes per the policy above. Every
    //   step is best-effort and a failure simply disables the SD sink for this
    //   session (UDP keeps working).
    void sdAppendLine(const char *text, size_t len);
    void sdRotate();

    Controller *controller = nullptr;
    QueueHandle_t queue = nullptr;
    TaskHandle_t taskHandle = nullptr;
    WiFiUDP udp;
    bool installed = false;

    // SD sink state (drain-task-owned; no cross-task sharing).
    bool sdEnabled = false;  // diagnostics on AND an SD card is mounted
    File sdFile;             // active rotating log file (invalid until first open)
    size_t sdBytes = 0;      // bytes written to the active file so far
    size_t sdSinceFlush = 0; // lines appended since the last flush
    uint32_t sdLastFlushMs = 0;

    // The vprintf this plugin replaced. Called from the hook so UART output is
    // preserved. Captured from esp_log_set_vprintf()'s return value.
    static vprintf_like_t previousVprintf;
    static DiagnosticLogPlugin *instance;
};

#endif // DIAGNOSTICLOGPLUGIN_H
