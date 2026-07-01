#include "WebUIPlugin.h"
#include <DNSServer.h>
#include <LittleFS.h>
#include <display/core/Controller.h>
#include <display/core/GrinderManager.h>
#include <display/core/HeapDiag.h>
#include <display/core/ProfileManager.h>
#include <display/core/SslRelayStartupPolicy.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/core/utils.h>
#include <display/models/profile.h>
#include <display/plugins/OtaCheckPolicy.h>
#include <display/plugins/PsramAllocator.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <SD_MMC.h>
#include <algorithm>
#include <cmath>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ChangeModeDeferPolicy.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/plugins/WsBroadcastClosePolicy.h>
#include <display/plugins/WsReassemblyPolicy.h>
#include <display/webassets/web_ui_manifest.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <version.h>

// PRO-358: the per-client WebSocket reassembly buffer holds decoded
// application-layer control-message bytes (small JSON req:/res:/evt: messages,
// profile saves at most a few KB). These bytes are CPU-accessed only — never a
// DMA source/target — so the buffer's backing store is a safe candidate to
// route OFF the scarce internal DMA-capable DRAM pool (MALLOC_CAP_INTERNAL) and
// into external PSRAM (MALLOC_CAP_SPIRAM). Under HomeKit + BLE + WiFi + mDNS the
// internal pool is what starves the OTA handshake below its 48 KB floor
// (OtaCheckPolicy.h); moving this multi-KB-capable buffer to PSRAM reclaims that
// internal headroom. PsramString == std::basic_string with a PSRAM-backed
// allocator on the device; on host/sim it degrades to a plain std::string (see
// PsramAllocator.h), so the reassembly logic is unchanged everywhere.
using PsramString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;
static std::unordered_map<uint32_t, PsramString> rxBuffers;
static WebUIPlugin *g_webUIPlugin = nullptr;

// Sentinel value emitted on /api/settings GET in place of any stored secret
// (Wi-Fi password, Home Assistant password, cloud relay token). The POST
// handler treats incoming arguments equal to this string as "no change",
// preserving the stored value. Defined as a constexpr so the same literal is
// used at every read/write site.
static constexpr const char *kSecretSentinel = "---unchanged---";

WebUIPlugin::WebUIPlugin() : server(80), ws("/ws") { g_webUIPlugin = this; }

// Parse wss://host[:port][/path] or ws://host[:port][/path]
static bool parseRelayUrl(const String &url, bool &useSSL, String &host, uint16_t &port, String &basePath) {
    if (url.startsWith("wss://")) {
        useSSL = true;
        String rest = url.substring(6);
        int slashIdx = rest.indexOf('/');
        String hostPort = (slashIdx < 0) ? rest : rest.substring(0, slashIdx);
        basePath = (slashIdx < 0) ? String("/") : rest.substring(slashIdx);
        int colonIdx = hostPort.indexOf(':');
        if (colonIdx < 0) {
            host = hostPort;
            port = 443;
        } else {
            host = hostPort.substring(0, colonIdx);
            port = (uint16_t)hostPort.substring(colonIdx + 1).toInt();
        }
        return true;
    }
    if (url.startsWith("ws://")) {
        useSSL = false;
        String rest = url.substring(5);
        int slashIdx = rest.indexOf('/');
        String hostPort = (slashIdx < 0) ? rest : rest.substring(0, slashIdx);
        basePath = (slashIdx < 0) ? String("/") : rest.substring(slashIdx);
        int colonIdx = hostPort.indexOf(':');
        if (colonIdx < 0) {
            host = hostPort;
            port = 80;
        } else {
            host = hostPort.substring(0, colonIdx);
            port = (uint16_t)hostPort.substring(colonIdx + 1).toInt();
        }
        return true;
    }
    return false;
}

void WebUIPlugin::addCorsHeaders(AsyncWebServerResponse *response) const {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    response->addHeader("Access-Control-Max-Age", "86400");
}

void WebUIPlugin::handleOptions(AsyncWebServerRequest *request) const {
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCorsHeaders(response);
    request->send(response);
}

// Forward declarations of channel helpers defined below.
static String resolveReleaseUrl(const String &channel);
static String normalizeChannel(const String &channel);

void WebUIPlugin::setup(Controller *_controller, PluginManager *_pluginManager) {
    this->controller = _controller;
    this->beanManager = _controller->getBeanManager();
    this->grinderManager = _controller->getGrinderManager();
    this->profileManager = _controller->getProfileManager();
    this->pluginManager = _pluginManager;
    this->ota = new GitHubOTA(
        BUILD_GIT_VERSION, controller->getSystemInfo().version, resolveReleaseUrl(controller->getSettings().getOTAChannel()),
        [this](uint8_t phase) {
            pluginManager->trigger("ota:update:phase", "phase", phase);
            updateOTAProgress(phase, 0);
        },
        [this](uint8_t phase, int progress) {
            pluginManager->trigger("ota:update:progress", "progress", progress);
            updateOTAProgress(phase, progress);
        },
        "display-firmware.bin", "display-filesystem.bin", "board-firmware.bin");
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        apMode = event.getInt("AP");
        // PRO-333: the SoftAP-fallback watchdog fires this event from the Arduino
        // main loop task (Controller::loop() -> wifiWatchdog()), unlike the normal
        // connect events (setupWifi on the setup task, the STA_GOT_IP handler on the
        // arduino_events WiFi-event task). Calling start() inline here would run
        // server.begin()/end(), ws.closeAll() and startRelay() on the loop task,
        // adding a task affinity the AsyncTCP/AsyncWebServer side is not guarded for
        // and violating the documented start()/stop() task-context invariant. The
        // watchdog tags its trigger with `deferred=1`; for that case just raise the
        // latch and let loop() invoke start() from its deferred-intent context. The
        // captive-portal UI still comes up — one loop tick later, on the loop task.
        if (event.getInt("deferred")) {
            pendingApRearm = true;
            return;
        }
        start();
    });
    pluginManager->on("controller:wifi:disconnect", [this](Event const &) { stop(); });
    pluginManager->on("controller:ready", [this](Event const &) {
        ota->setControllerVersion(controller->getSystemInfo().version);
        ota->init(controller->getClientController()->getClient());
    });
    pluginManager->on("controller:autotune:result", [this](Event const &event) { sendAutotuneResult(); });

    // Forward shot history rebuild progress events to WebSocket clients
    pluginManager->on("evt:history-rebuild-progress", [this](Event const &event) {
        JsonDocument doc;
        doc["tp"] = "evt:history-rebuild-progress";
        doc["total"] = event.getInt("total");
        doc["current"] = event.getInt("current");
        doc["status"] = event.getString("status");
        broadcastAll(doc.as<String>());
    });

    // Subscribe to Bluetooth scale weight updates
    pluginManager->on("controller:volumetric-measurement:bluetooth:change",
                      [this](Event const &event) { this->currentBluetoothWeight = event.getFloat("value"); });

    // Create the relay lifecycle mutex once here, before any WiFi/server events
    // can fire, so startRelay()/stopRelay() are serialized across the
    // arduino_events and AsyncTCP tasks (CAR-259).
    if (relayLifecycleMutex == nullptr) {
        relayLifecycleMutex = xSemaphoreCreateMutex();
    }

    // Guards the pendingReleaseUrl String handoff between WS/relay-task handlers
    // and the loop task (CAR-178). Created here, before any WiFi/server events
    // can fire a WS handler that posts OTA intent.
    if (otaIntentMutex == nullptr) {
        otaIntentMutex = xSemaphoreCreateMutex();
    }

    // Serializes all `ws` client-list access between loopTask and the AsyncTCP
    // task (PRO-313). MUST be created before setupServer() registers ws.onEvent
    // and addHandler(&ws) below, so the mutex already exists the first time a
    // client connects on the AsyncTCP task. See the invariant at the `ws`
    // declaration in WebUIPlugin.h.
    if (wsMutex == nullptr) {
        wsMutex = xSemaphoreCreateMutex();
    }

    setupServer();
}

void WebUIPlugin::relayLoopTask(void *arg) {
    auto *plugin = static_cast<WebUIPlugin *>(arg);
    while (true) {
        // Cooperative shutdown: stopRelay() requests exit, and the teardown
        // runs here on the task that owns the WebSocket allocations / mbedTLS
        // state, never via vTaskDelete on a remote handle mid-loop (CAR-259).
        if (plugin->relayTaskExitRequested.load(std::memory_order_acquire)) {
            plugin->relayWs.disconnect();
            plugin->relayConnected = false;
            // Publish NULL before self-deleting so stopRelay()'s wait observes
            // the task is gone. The release store pairs with stopRelay()'s
            // acquire load so the relayWs.disconnect() / relayConnected teardown
            // above happens-before the observer sees the null handle. After
            // vTaskDelete(NULL) this task never runs again, so no further access
            // to plugin state occurs.
            plugin->relayTaskHandle.store(nullptr, std::memory_order_release);
            vTaskDelete(nullptr);
            return; // unreachable; keeps the compiler happy
        }
        if (plugin->relayEnabled && plugin->relayMutex != nullptr) {
            if (plugin->relayConnected) {
                std::vector<String> toSend;
                if (xSemaphoreTake(plugin->relayMutex, 0) == pdTRUE) {
                    toSend.swap(plugin->relayOutBuffer);
                    xSemaphoreGive(plugin->relayMutex);
                }
                for (auto &msg : toSend) {
                    plugin->relayWs.sendTXT(msg);
                }
            }
            plugin->relayWs.loop();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

namespace {
// Shared RAII guard for a non-recursive FreeRTOS mutex (PRO-314, unifying the
// former WsClientsLock from PRO-313 and RelayLifecycleLock from CAR-259). Takes
// the handle on construction (if it exists) and gives it back on destruction,
// so every critical section is bracketed without a hand-audited give at each
// return path. Blocks (portMAX_DELAY) on acquire and degrades to a no-op when
// the handle is nullptr.
//
// INVARIANTS (callers must uphold; see per-site comments):
//   * Non-recursive: never construct a second guard for the same mutex while
//     one is already live on this task.
//   * No lock-order inversion: the guarded mutexes (wsMutex, relayLifecycleMutex)
//     never nest with each other or with relayMutex; each critical section takes
//     exactly one of them, so portMAX_DELAY cannot deadlock.
//   * NO UNBOUNDED BLOCKING under this lock: because acquire is portMAX_DELAY,
//     any unbounded blocking operation inside the locked scope (network I/O,
//     taking another lock, a portMAX_DELAY-style wait) would turn into a hard
//     hang. Keep critical sections short; only a strictly BOUNDED, CPU-yielding
//     wait is acceptable (e.g. the ~500 ms vTaskDelay spin-wait in stopRelay()).
//     Such a bounded in-lock wait must STILL NOT acquire another guarded mutex
//     in-scope: it remains bound by the no-lock-order-inversion invariant above
//     (these guards never nest with each other or with relayMutex).
struct SemaphoreGuard {
    SemaphoreHandle_t handle;
    explicit SemaphoreGuard(SemaphoreHandle_t h) : handle(h) {
        if (handle != nullptr) {
            xSemaphoreTake(handle, portMAX_DELAY);
        }
    }
    ~SemaphoreGuard() {
        if (handle != nullptr) {
            xSemaphoreGive(handle);
        }
    }
    SemaphoreGuard(const SemaphoreGuard &) = delete;
    SemaphoreGuard &operator=(const SemaphoreGuard &) = delete;
};
} // namespace

void WebUIPlugin::loop() {
    // Latch deferred OTA-start intent posted by handleOTAStart on the WS/relay
    // task (CAR-377). Runs on the loop task, so `updating` and `updateComponent`
    // are written and read only here. If contended, leave pendingOtaStart set so
    // the next iteration retries — no queued start is lost.
    //
    // ORDERING (do not reorder): this latch MUST stay above the `if (updating)`
    // block (so a freshly-latched start runs in the same iteration) and above the
    // `if (!serverRunning) return;` guard below (so a queued start is not stranded
    // while in AP mode / before the server is up).
    if (pendingOtaStart) {
        if (otaIntentMutex != nullptr && xSemaphoreTake(otaIntentMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            updateComponent = pendingUpdateComponent;
            pendingUpdateComponent = "";
            pendingOtaStart = false;
            updating = true;
            xSemaphoreGive(otaIntentMutex);
        }
    }
    // Drain the deferred mode-change intent (PRO-261). Runs on the Arduino main
    // loop task — the only task that touches Controller mode/process state for
    // this deferral — so it mirrors DefaultUI::loop's pendingAutoSteam gate. Kept
    // above the `if (!serverRunning) return;` guard below so a request that
    // arrived over the cloud relay (no local AsyncWebServer running) still
    // applies once the settle window closes.
    if (pendingModeChange) {
        // "Still hold" is the same defer decision as the arming gate (PRO-267):
        // a deferral is only ever armed for non-STANDBY targets, so
        // shouldDeferModeChange(pendingModeChangeTarget, ...) reduces to the
        // settle-window check here — sharing the predicate keeps the hold/apply
        // decision in lock-step with the arming gate.
        if (shouldDeferModeChange(pendingModeChangeTarget, ShotHistory.isExtendedRecording())) {
            // Settle still in progress: hold the mode, keep the BLE scale connected
            // and record() logging so post-stop drips land in the yield. Re-checked
            // next iteration (~2 ms) — no blocking wait, no delay() on this task.
        } else {
            const uint8_t target = pendingModeChangeTarget;
            pendingModeChange = false;
            // Window closed (settle finished, scale went unhealthy, or there was no
            // scale at all). deactivate() leaves mode == MODE_BREW after a normal
            // shot; this guard is false only if the user explicitly navigated away
            // (e.g. to standby) between brew-end and now, in which case discarding
            // the deferred transition is the correct, display-matching behavior.
            if (controller->getMode() == MODE_BREW) {
                controller->clear();
                controller->setMode(target);
            }
        }
    }
    if (updating) {
        pluginManager->trigger("ota:update:start");
        // Force-flash whenever the user pinned a specific tag (e.g. "tag:2.0.8").
        // This bypasses the upgrade-only guard so re-flashing the same version
        // and downgrading both work.
        const String channel = controller->getSettings().getOTAChannel();
        const bool force = channel.startsWith("tag:");
        bool tagResolved = true;
        if (force) {
            // Defense-in-depth: a WS client can send `req:ota-settings tag:X`
            // followed immediately by `req:ota-start` before the throttled
            // checkForUpdates() in this same loop runs (the if-blocks in
            // loop() are sequential, and the OTA-start arm executes first).
            // In that race `_release_url` points at tag/X but `_latest_url`
            // still holds the previous channel's resolved URL, so a forced
            // update would flash the wrong asset.
            //
            // Resolve `_latest_url` synchronously here, then verify the
            // freshly-resolved version equals the pinned tag. If it doesn't
            // (network error, GitHub redirect quirk, malformed channel), we
            // refuse the update — never flash a tag we can't confirm.
            const String pinned = channel.substring(4);
            ota->checkForUpdates();
            const String resolved = ota->getCurrentVersion();
            // GitHub release tags occasionally carry a leading `v` prefix
            // (`v1.8.2`); the resolver strips it, but the channel string we
            // stored does not. Treat them as equal so legacy tags still flash.
            // Cover both directions in case a future resolver path keeps the
            // `v` and the channel string drops it.
            const bool match = resolved == pinned || (pinned.startsWith("v") && resolved == pinned.substring(1)) ||
                               (resolved.startsWith("v") && resolved.substring(1) == pinned);
            if (!match) {
                ESP_LOGE("WebUIPlugin", "Refusing forced OTA: pinned tag %s but resolved %s", pinned.c_str(), resolved.c_str());
                tagResolved = false;
            }
        }
        bool updateSucceeded = false;
        if (tagResolved) {
            updateSucceeded = ota->update(updateComponent != "display", updateComponent != "controller", force);
        }
        pluginManager->trigger("ota:update:end");
        updating = false;
        if (!updateSucceeded) {
            updateOTAStatus(tagResolved ? "Update failed" : "Update failed (tag not resolved)");
        }
    }

    // Drain the deferred SoftAP-re-arm intent posted by the WiFi watchdog
    // (PRO-333). The watchdog's OPEN_SOFTAP branch runs on this same loop task but
    // tags its connect event `deferred=1` so the handler only latches here instead
    // of calling start() synchronously from inside wifiWatchdog(). Draining it here
    // keeps every start() invocation on the loop task's deferred-intent context,
    // matching pendingOtaStart above. apMode was already set by the connect handler
    // before this flag was raised, so start() opens the captive-portal DNS path.
    //
    // ORDERING: kept above the `if (!serverRunning) return;` guard below so the AP
    // re-arm still runs after a prior stop() left serverRunning false (the watchdog
    // fires precisely when the STA link dropped, which may have torn the server
    // down via controller:wifi:disconnect).
    if (pendingApRearm) {
        pendingApRearm = false;
        start();
    }
    if (!serverRunning) {
        return;
    }
    // Drain deferred OTA intent posted by WS/relay-task handlers (CAR-178).
    // Runs on the loop task, so these are the only task touching `ota` here. If
    // a release-URL change arrived while an update() was in flight above, it
    // applies now — after the in-flight update completed — which is the intended
    // "applied after completion" behavior rather than switching URLs mid-stream.
    if (pendingReleaseUrlChange) {
        String url;
        bool emptyHandoff = false;
        bool have = false;
        if (otaIntentMutex != nullptr && xSemaphoreTake(otaIntentMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            url = pendingReleaseUrl;
            emptyHandoff = url.isEmpty();
            pendingReleaseUrl = ""; // release the copy; the flag is the source of truth
            pendingReleaseUrlChange = false;
            have = true;
            xSemaphoreGive(otaIntentMutex);
        }
        if (have) {
            // emptyHandoff means the handler couldn't take the mutex to store the
            // resolved URL (the contended drop path) and only raised the flag, OR
            // a channel was persisted without an explicit URL. Re-resolve from the
            // persisted channel here on the loop task — checkForUpdates() reads
            // _release_url to derive _latest_url but never re-derives _release_url
            // itself, so without this the new channel would never reach `ota`.
            if (emptyHandoff) {
                url = resolveReleaseUrl(controller->getSettings().getOTAChannel());
            }
            ota->setReleaseUrl(url);
        }
    }
    // NOTE: the status-push drain MUST stay after the release-URL drain above, so
    // the "Checking..." broadcast reflects the just-applied channel rather than
    // the previous one (CAR-178). Do not reorder these two blocks.
    if (pendingOtaStatusPush) {
        pendingOtaStatusPush = false;
        updateOTAStatus("Checking...");
    }
    const unsigned long now = millis();
    // PRO-345: decide run/defer/skip via the host-testable OtaCheckPolicy (single
    // source of truth for the floors + cadences). PRO-334 gated the OTA HTTPS
    // version-check on internal-DRAM headroom because the mbedTLS handshake's
    // transient internal allocation can -32512 under HomeKit + BLE + WiFi + mDNS.
    // But on this hardware that normal steady state sits BELOW the 48 KB floor, so
    // the old "defer every interval, never advance lastUpdateCheck" path starved
    // forever: the UI stuck at "Checking..." with no update ever offered.
    //
    // The policy keeps the fast path (>= preferred floor -> run every interval)
    // AND makes the defer RECOVERABLE: below the floor it still attempts on a
    // longer escalated cadence, as long as the largest block clears a hard
    // absolute-minimum floor (the OOM guard preserving PRO-334's -32512
    // protection). When it returns Defer we surface a truthful, distinct status
    // instead of leaving the UI at "Checking...". A successful Run replaces that
    // status with the real result (criterion 2). lastUpdateCheck is advanced only
    // on an actual Run, so the escalated timer keeps maturing across defers.
    const size_t largestBlock = gmInternalLargestBlock();
    const OtaCheckDecision decision =
        otaCheckDecision(largestBlock, now, lastUpdateCheck, UPDATE_CHECK_INTERVAL, kOtaCheckEscalatedRetryIntervalMs,
                         kOtaCheckInternalDramFloorBytes, kOtaCheckAbsoluteMinInternalDramBytes);
    if (decision == OtaCheckDecision::Run) {
        GM_LOG_INTERNAL_DRAM("before OTA TLS check");
        ota->checkForUpdates();
        GM_LOG_INTERNAL_DRAM("after OTA TLS check");
        pluginManager->trigger("ota:update:status", "value", ota->isUpdateAvailable());
        lastUpdateCheck = now;
        lastOtaDeferNotice = 0; // a real result replaces any prior deferred status
        updateOTAStatus(ota->getCurrentVersion());
    } else if (decision == OtaCheckDecision::Defer) {
        // Below the preferred floor and not yet time for an escalated attempt (or
        // below the OOM guard): do NOT drive the handshake. Surface a truthful
        // status so the UI no longer hangs at "Checking..." — it will be replaced
        // by the real result on the next successful (escalated) Run. lastUpdateCheck
        // is intentionally NOT advanced (keeps the escalated timer maturing), so we
        // throttle the broadcast to at most once per interval rather than every loop.
        if (lastOtaDeferNotice == 0 || now - lastOtaDeferNotice > UPDATE_CHECK_INTERVAL) {
            lastOtaDeferNotice = now;
            ESP_LOGW(
                "WebUIPlugin",
                "Deferring OTA check: internal DRAM below floor (largest block=%u B < %u B); will retry on escalated cadence",
                static_cast<unsigned>(largestBlock), static_cast<unsigned>(kOtaCheckInternalDramFloorBytes));
            updateOTAStatus("Update check deferred — low memory");
        }
    }
    // PRO-313: reading the WS client list (even .empty()) races with the
    // AsyncTCP task's connect/disconnect mutation, so snapshot it under wsMutex.
    // The && short-circuits on the cheap STATUS_PERIOD timer first, so the lock
    // is taken at most once per status interval, not every loop pass. The lambda
    // keeps the lock scoped to the O(1) read; the JSON build below touches no ws
    // state and broadcastAll() re-takes the lock for the actual send.
    if (now - lastStatus > STATUS_PERIOD && ([this] {
            SemaphoreGuard lock(wsMutex);
            return !ws.getClients().empty();
        }() || relayConnected)) {
        lastStatus = now;
        JsonDocument doc;
        doc["tp"] = "evt:status";
        doc["ct"] = controller->getCurrentTemp();
        doc["tt"] = controller->getTargetTemp();
        doc["pr"] = controller->getCurrentPressure();
        doc["fl"] = controller->getCurrentPumpFlow();
        // Send null (not 0) when no target is applicable in the current mode —
        // e.g. a simple-pump profile in standby has no pressure/flow target — so
        // the web UI can fall back to its default instead of showing a false 0.
        if (controller->hasTargetPressure()) {
            doc["pt"] = controller->getTargetPressure();
        } else {
            doc["pt"] = nullptr;
        }
        if (controller->hasTargetFlow()) {
            doc["tf"] = controller->getTargetFlow();
        } else {
            doc["tf"] = nullptr;
        }
        doc["m"] = controller->getMode();
        doc["p"] = controller->getProfileManager()->getSelectedProfile().label;
        doc["puid"] = controller->getProfileManager()->getSelectedProfile().id;
        doc["bn"] = controller->getSettings().getSelectedBean();
        doc["cp"] = controller->getSystemInfo().capabilities.pressure;
        doc["cd"] = controller->getSystemInfo().capabilities.dimming;
        doc["tw"] = profileManager->getSelectedProfile().getTotalVolume(); // total target weight for the process
        doc["bta"] = controller->isVolumetricAvailable() ? 1 : 0;
        doc["bt"] =
            controller->isVolumetricAvailable() && controller->getProfileManager()->getSelectedProfile().isVolumetric() ? 1 : 0;
        doc["btd"] = profileManager->getSelectedProfile().getTotalDuration();
        doc["btv"] = profileManager->getSelectedProfile().getTotalVolume(); // raw volumetric target for frontend Weight card
        doc["ayo"] = controller->getSettings().isAllowYieldOverride() ? 1 : 0;
        doc["as"] = controller->getSettings().isAutoSteamEnabled() ? 1 : 0;
        // Round dose grams to 1 decimal so the wire value matches the "float" web contract
        // (avoids noisy full-precision doubles; firmware keeps full precision internally).
        doc["dg"] = std::round(controller->getSettings().getDoseGrams() * 10.0) / 10.0;
        doc["led"] = controller->getSystemInfo().capabilities.ledControl;
        doc["gtd"] = controller->getTargetGrindDuration();
        doc["gtv"] = controller->getSettings().getTargetGrindVolume();
        doc["gt"] = controller->isVolumetricAvailable() && controller->getSettings().isVolumetricTarget() ? 1 : 0;
        doc["gact"] = controller->isGrindActive() ? 1 : 0;
        doc["mtp"] = controller->getManualTargetType() == MANUAL_TARGET_FLOW ? "flow" : "pressure";
        doc["mp"] = controller->getManualPressure();
        doc["mf"] = controller->getManualFlow();
        doc["mt"] = controller->getManualTemperature();
        doc["rssi"] = -127;
        if (controller->getClientController()->getClient()->isConnected()) {
            doc["rssi"] = controller->getClientController()->getClient()->getRssi();
        }

#if GAGGIMATE_ENABLE_BLE_SCALE
        bool bleConnected = BLEScales.isConnected();
        // Add Bluetooth scale weight information
        doc["cw"] = bleConnected ? this->currentBluetoothWeight : 0; // current bluetooth weight
        doc["bc"] = bleConnected;                                    // bluetooth scale connected status
#else
        // BLE scale compiled out (CAR-382): always report disconnected / zero
        // weight. Volumetric still works via flow estimation; that value flows
        // through the process snapshot, not these BLE-specific status fields.
        doc["cw"] = 0;     // current bluetooth weight
        doc["bc"] = false; // bluetooth scale connected status
#endif

        // Use thread-safe snapshot to avoid use-after-free race conditions
        ProcessSnapshot proc = controller->getProcessSnapshot();
        if (proc.exists) {
            auto pObj = doc["process"].to<JsonObject>();
            // Add current shot ID so frontend can attach dose data to shot notes
            // Only send when recording to avoid empty IDs when no shot is active
            String shotId = ShotHistory.getCurrentShotId();
            if (ShotHistory.isRecording() && !shotId.isEmpty()) {
                pObj["id"] = shotId;
            }
            // Use snapshot state only to avoid TOCTOU race condition
            pObj["a"] = proc.isActive ? 1 : 0;
            if (proc.isBrew) {
                // Use snapshot state consistently - no redundant isActive() call
                unsigned long ts = proc.isActive ? millis() : proc.finished;
                pObj["s"] = proc.phaseType == static_cast<int>(PhaseType::PHASE_TYPE_BREW) ? "brew" : "infusion";
                pObj["l"] = proc.isActive ? proc.phaseName.c_str() : "Finished";
                pObj["e"] = ts - proc.started;
                const bool isVolumetric =
                    proc.target == ProcessTarget::VOLUMETRIC && proc.hasVolumetricTarget && controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    pObj["pt"] = proc.volumetricTargetValue;
                    pObj["pp"] = proc.currentVolume;
                } else {
                    pObj["pt"] = proc.phaseDuration;
                    pObj["pp"] = ts - proc.currentPhaseStarted;
                }
            } else if (proc.isGrind) {
                // Use snapshot state consistently - no redundant isActive() call
                unsigned long ts = proc.isActive ? millis() : proc.finished;
                pObj["s"] = "grind";
                pObj["l"] = proc.isActive ? "Grinding" : "Finished";
                pObj["e"] = ts - proc.started;
                const bool isVolumetric = proc.target == ProcessTarget::VOLUMETRIC && controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    pObj["pt"] = proc.grindVolume;
                    pObj["pp"] = proc.currentVolume;
                } else {
                    pObj["pt"] = proc.grindTime;
                    pObj["pp"] = ts - proc.started;
                }
            } else if (proc.isManual) {
                unsigned long ts = proc.isActive ? millis() : proc.finished;
                pObj["s"] = "manual";
                pObj["l"] = proc.isActive ? "Manual" : "Finished";
                pObj["e"] = ts - proc.started;
                pObj["tt"] = proc.manualTargetType == MANUAL_TARGET_FLOW ? "flow" : "pressure";
                pObj["pt"] = proc.manualTargetType == MANUAL_TARGET_FLOW ? proc.manualFlow : proc.manualPressure;
                pObj["pp"] = proc.manualTargetType == MANUAL_TARGET_FLOW ? controller->getCurrentPumpFlow()
                                                                         : controller->getCurrentPressure();
            }
        }

        broadcastAll(doc.as<String>());
    }
    if (now - lastCleanup > CLEANUP_PERIOD) {
        lastCleanup = now;
        // PRO-313: cleanupClients() walks AND erases the client list; serialize
        // it against the AsyncTCP task's connect/disconnect mutation.
        SemaphoreGuard lock(wsMutex);
        ws.cleanupClients();
    }
    if (now - lastDns > DNS_PERIOD && dnsServer != nullptr) {
        lastDns = now;
        dnsServer->processNextRequest();
    }
}

// Linear lookup over the embedded asset table (~50 entries) — a couple of
// strcmps per request, negligible next to the network round-trip.
static const WebAsset *findWebAsset(const String &path) {
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (path == WEB_ASSETS[i].path) {
            return &WEB_ASSETS[i];
        }
    }
    return nullptr;
}

void WebUIPlugin::serveWebAsset(AsyncWebServerRequest *request) { serveWebAsset(request, request->url()); }

void WebUIPlugin::serveWebAsset(AsyncWebServerRequest *request, String path) {
    if (path.isEmpty() || path == "/") {
        path = WEB_UI_INDEX_PATH;
    }

    const WebAsset *asset = findWebAsset(path);
    if (asset == nullptr && !path.startsWith("/assets/")) {
        // SPA client-side routes (e.g. /settings, /profiles) aren't real files —
        // fall back to index.html. A miss under /assets/ is a genuine 404, not a
        // route, so it is not rewritten.
        asset = findWebAsset(WEB_UI_INDEX_PATH);
    }
    if (asset == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
    }

    // Serve straight from the memory-mapped flash blob — no copy into RAM, no
    // filesystem read.
    AsyncWebServerResponse *response =
        request->beginResponse(200, asset->contentType, gWebUiBlobStart + asset->offset, asset->length);
    if (asset->gzip) {
        response->addHeader("Content-Encoding", "gzip");
    }
    // Long-lived immutable cache for build assets. /assets/* are content-hashed (the URL changes per build); /fonts/*
    // are stable-named .otf files busted by a firmware version bump / fresh install — same policy the prior
    // serveStatic("/fonts/", ...) used. index.html and other unhashed top-level files must revalidate so a new build
    // is picked up after an update. [GM-83]
    if (path.startsWith("/assets/") || path.startsWith("/fonts/")) {
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    } else {
        response->addHeader("Cache-Control", "no-cache");
    }
    request->send(response);
}

void WebUIPlugin::setupServer() {
    server.on("^\\/api\\/.*$", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) { handleOptions(request); });
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
        request->redirect("http://logout.net");
    }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) {
        request->send(404);
    }); // Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32
        // :)
    server.on("/generate_204",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // android captive portal redirect
    server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });            // microsoft redirect
    server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // apple call home
    server.on("/canonical.html",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });       // firefox captive portal call home
    server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // windows call home
    server.on("/api/settings", [this](AsyncWebServerRequest *request) { handleSettings(request); });
    server.on("/api/status", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        addCorsHeaders(response);
        JsonDocument doc;
        doc["mode"] = controller->getMode();
        doc["tt"] = controller->getTargetTemp();
        doc["ct"] = controller->getCurrentTemp();
        serializeJson(doc, *response);
        request->send(response);
    });
    // BLE-scale HTTP surface. The routes are registered UNCONDITIONALLY so a
    // flags-off build (GAGGIMATE_ENABLE_BLE_SCALE=0) still answers them: the
    // prebuilt web bundle fetches /api/scales/* on load regardless of the
    // device's compiled feature set, and a 404 there shows "Error loading
    // devices" instead of a clean "no scales" state. With BLE compiled out the
    // handlers (see the #else block in handleBLEScale*) return a typed empty
    // payload (list: [], info: {connected:false,...}, scan/connect:
    // {success:false}) with HTTP 200 so the client renders an empty list
    // (CAR-386). The default BLE-on behavior is unchanged.
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) { handleBLEScaleList(request); });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) { handleBLEScaleConnect(request); });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) { handleBLEScaleScan(request); });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) { handleBLEScaleInfo(request); });
    FS *fs = &LittleFS;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
    }
    server.serveStatic("/api/history/", *fs, "/h/").setCacheControl("no-store");
    server.on("/api/history/index.bin", HTTP_GET, [this, fs](AsyncWebServerRequest *request) {
        // Serve the binary index file directly
        if (fs->exists("/h/index.bin")) {
            AsyncWebServerResponse *response = request->beginResponse(*fs, "/h/index.bin", "application/octet-stream");
            addCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "text/plain", "Index not found");
        }
    });
    server.on("/api/core-dump", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCoreDumpDownload(request); });
    // Diagnostic SD log download (PRO-274). Explicit handlers (not serveStatic)
    // so a missing file returns a clean 404 instead of falling through to the
    // SPA catch-all (onNotFound) below. Registered before onNotFound so these
    // /api/-prefixed routes win. Served regardless of the diagnosticLog flag —
    // the file may exist from a prior enabled session — but gated on a mounted
    // SD card inside the handler. Paths mirror DiagnosticLogPlugin::SD_LOG_PATH /
    // SD_LOG_PATH_OLD; literals are used here rather than including that plugin's
    // header (it pulls WiFiUdp.h, which the native display-sim build can't resolve).
    server.on("/api/diag/log.txt", HTTP_GET,
              [this](AsyncWebServerRequest *request) { handleDiagLogDownload(request, "/diag/log.txt"); });
    server.on("/api/diag/log.1", HTTP_GET,
              [this](AsyncWebServerRequest *request) { handleDiagLogDownload(request, "/diag/log.1"); });
    server.on("/test", [](AsyncWebServerRequest *request) {
        ESP_LOGI("WebUI", "TEST endpoint hit!");
        request->send(200, "text/plain", "ESP32 server is alive!");
    });
    // Favicon / touch icons are served from the embedded gm.png blob (kept out of the filesystem). [GM-106]
    server.on("/favicon.ico", [this](AsyncWebServerRequest *request) { serveWebAsset(request, "/gm.png"); });
    server.on("/apple-touch-icon.png", [this](AsyncWebServerRequest *request) { serveWebAsset(request, "/gm.png"); });
    server.on("/apple-touch-icon-precomposed.png", [this](AsyncWebServerRequest *request) { serveWebAsset(request, "/gm.png"); });
    // The web UI is embedded in firmware flash and served from the memory-mapped blob (see serveWebAsset). It is no
    // longer in LittleFS, so OTA never touches the partition holding profiles/shots. The catch-all onNotFound handles
    // every path not claimed by an explicit server.on()/api route above (/, /assets/*, /fonts/*, SPA routes). [GM-106]
    server.onNotFound([this](AsyncWebServerRequest *request) { serveWebAsset(request); });
    ws.onEvent(
        [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            // PRO-313: this callback runs on the AsyncTCP task. The
            // server->getClients() reads below walk the same client list that
            // loopTask broadcasts over, so they must hold wsMutex too — the
            // lock only serializes the two tasks if BOTH sides take it. The
            // WS_EVT_DATA path takes wsMutex itself inside sendResponse(), so it
            // is intentionally NOT wrapped here (wsMutex is non-recursive;
            // double-taking on this task would deadlock). NOTE: the structural
            // emplace_back (connect) / erase (disconnect) on _clients happens in
            // ESPAsyncWebServer's own code immediately before this callback
            // fires, so it cannot be bracketed from here; serializing every
            // app-side walk is the complete fix this library permits without
            // forking it. See the invariant at the `ws` declaration.
            if (type == WS_EVT_CONNECT) {
                // PRO-357: leave setCloseClientOnQueueFull(false). The library's
                // inline close-on-queue-full would re-enter WS_EVT_DISCONNECT on the
                // sending task and self-deadlock the non-recursive wsMutex held by
                // broadcastAll() -> ws.textAll(). Full rationale (the coredump, the
                // fix, and the safety predicate's params) lives in
                // WsBroadcastClosePolicy.h.
                static_assert(!wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/true,
                                                              /*mutexIsRecursive=*/false),
                              "WS broadcast sends under a non-recursive wsMutex: inline close-on-queue-full would "
                              "re-enter the disconnect handler and self-deadlock (PRO-357)");
                // The library DEFAULT is closeWhenFull == true (AsyncWebSocket.h),
                // so the safe behaviour the comment+static_assert describe is NOT
                // the field's default state — it must be established explicitly on
                // every client. Without this runtime call a full TX queue would
                // still inline-close and re-open PRO-357. The static_assert above
                // is only a compile-time check of the policy predicate; it has
                // zero effect on closeWhenFull and cannot substitute for this
                // setter. Keep this call in lockstep with the predicate.
                client->setCloseClientOnQueueFull(false);
                // Anchor the invariant to the ACTUAL client configuration so a
                // future deletion/regression of the setter above is caught at the
                // real call site (the host test pins the pure predicate; this pins
                // the runtime field). willCloseClientOnQueueFull() must report the
                // same "safe" decision the predicate encodes.
                if (client->willCloseClientOnQueueFull() != wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/true,
                                                                                           /*mutexIsRecursive=*/false)) {
                    ESP_LOGE("WebUIPlugin",
                             "PRO-357 invariant broken: WS client inline close-on-queue-full is %d but the broadcast "
                             "path requires it disabled; a full TX queue would self-deadlock wsMutex",
                             client->willCloseClientOnQueueFull());
                }
                SemaphoreGuard lock(wsMutex);
                ESP_LOGI("WebUIPlugin", "WebSocket client connected (%d open connections)", server->getClients().size());
            } else if (type == WS_EVT_DISCONNECT) {
                {
                    SemaphoreGuard lock(wsMutex);
                    ESP_LOGI("WebUIPlugin", "WebSocket client disconnected (%d open connections)", server->getClients().size());
                }
                rxBuffers.erase(client->id());
            } else if (type == WS_EVT_DATA) {
                handleWebSocketData(server, client, type, arg, data, len);
            }
        });
    server.addHandler(&ws);
}

void WebUIPlugin::start() {
    stop();
    server.begin();
    ESP_LOGI("WebUIPlugin", "Started webserver");
    if (apMode) {
        dnsServer = new DNSServer();
        dnsServer->setTTL(3600);
        dnsServer->start(53, "*", WIFI_AP_IP);
        ESP_LOGI("WebUIPlugin", "Started catchall DNS for captive portal");
    }
    lastUpdateCheck = 0;
    lastOtaDeferNotice = 0; // PRO-345: allow a fresh defer notice on the forced recheck
    serverRunning = true;
    startRelay();
}

void WebUIPlugin::stop() {
    stopRelay();
    if (!serverRunning)
        return;
    server.end();
    {
        // PRO-313: closeAll() walks the client list and runs on the WiFi-event
        // task (start()/stop()), a third task distinct from loopTask and
        // AsyncTCP. Serialize it against their client-list access.
        SemaphoreGuard lock(wsMutex);
        ws.closeAll();
    }
    if (dnsServer != nullptr) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    // ota is owned by the plugin for its full lifetime: allocated once in
    // setup() and never freed. Freeing it here used to race with reads on
    // the Arduino loop task and the AsyncTCP/WS task (see CAR-100). It also
    // permanently nulled `ota` after the first WiFi cycle since start()
    // never reallocates it.
    serverRunning = false;
}

// The relay lifecycle mutex is taken via the shared SemaphoreGuard (defined
// near the top of this file). It takes the mutex on construction (if it exists)
// and gives it back on destruction, so every return path out of
// startRelay()/stopRelay() — early guards, the deferred-start/timeout returns,
// the SSL heap-guard, the OOM path, and the normal returns — releases the lock
// without a hand-audited give at each site (CAR-259). relayLifecycleMutex never
// nests with wsMutex or relayMutex, so its portMAX_DELAY acquire cannot deadlock.

void WebUIPlugin::startRelay() {
    // Caller-context: startRelay()/stopRelay() are invoked from two different
    // FreeRTOS tasks — start()/stop() run inline on the arduino_events WiFi-event
    // task (controller:wifi:connect/disconnect), while handleSettings() runs on the
    // AsyncTCP /api/settings task and calls stopRelay()+startRelay() back-to-back.
    // A WiFi (dis)connect can therefore genuinely interleave with a cloud-relay
    // settings toggle. They are now serialized by relayLifecycleMutex (taken at the
    // top of both functions, released on every return path) so the atomic-flag
    // handoff with relayLoopTask stays coherent and two starts can never both
    // observe relayTaskHandle==nullptr and orphan a live task (CAR-259). A plain
    // (non-recursive) mutex is correct: start() calls stop() then startRelay()
    // sequentially (not nested), and handleSettings() calls them sequentially too —
    // neither function calls the other while holding the lock.
    SemaphoreGuard lock(relayLifecycleMutex);
    const String &relayUrl = controller->getSettings().getCloudRelayUrl();
    const String &relayToken = controller->getSettings().getCloudRelayToken();
    if (relayUrl.isEmpty() || relayToken.isEmpty() || !controller->getSettings().isCloudRelayEnabled())
        return;

    bool useSSL;
    String host, basePath;
    uint16_t port;
    if (!parseRelayUrl(relayUrl, useSSL, host, port, basePath)) {
        ESP_LOGW("WebUIPlugin", "Invalid relay URL: %s", relayUrl.c_str());
        return;
    }

    if (relayMutex == nullptr) {
        relayMutex = xSemaphoreCreateMutex();
    }

    // If a prior stopRelay() timed out, the old relay task is still alive,
    // draining its in-flight relayWs.loop() with relayTaskExitRequested set; it
    // will run its own relayWs.disconnect() and vTaskDelete(NULL) imminently.
    // We must NOT reconfigure relayWs (onEvent / begin*) from this caller task
    // while that task may still touch it — that is a data race on the
    // WebSocketsClient (CAR-259). Wait briefly for the handle to clear, then
    // fall through to create a fresh task. If it has not cleared in time, skip
    // this start; the pending exit will complete and the next disconnect/
    // reconnect or settings change re-triggers startRelay() cleanly.
    if (relayTaskHandle.load(std::memory_order_acquire) != nullptr) {
        constexpr TickType_t drainInterval = pdMS_TO_TICKS(10);
        constexpr int maxDrainPolls = 50; // ~500 ms
        int drainPolls = 0;
        while (relayTaskHandle.load(std::memory_order_acquire) != nullptr && drainPolls < maxDrainPolls) {
            vTaskDelay(drainInterval);
            ++drainPolls;
        }
        if (relayTaskHandle.load(std::memory_order_acquire) != nullptr) {
            // NOTE(CAR-259): no internal retry timer here. This deferred start
            // relies on the next external re-trigger — a WiFi reconnect
            // (start()), a stop()/start() cycle, or another /api/settings toggle
            // (handleSettings()) — to call startRelay() again. The only path
            // that leaves the relay silently down is the double-timeout tail
            // (this drain AND the prior stopRelay() both exceeding ~500 ms) with
            // no subsequent WiFi/settings event, which is not expected in the
            // field (a wedged relayWs.loop() resolves well under 500 ms). Treat
            // the silent return as intentional, not a missing-retry bug.
            ESP_LOGW("WebUIPlugin", "Prior relay task still exiting; deferring start until it self-deletes");
            return;
        }
    }

    String path = (basePath.isEmpty() || basePath == "/") ? "/connect?token=" + relayToken + "&role=device"
                                                          : basePath + "/connect?token=" + relayToken + "&role=device";

    relayWs.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
        switch (type) {
        case WStype_CONNECTED:
            relayConnected = true;
            ESP_LOGI("WebUIPlugin", "Connected to cloud relay");
            break;
        case WStype_DISCONNECTED:
            relayConnected = false;
            ESP_LOGI("WebUIPlugin", "Disconnected from cloud relay");
            break;
        case WStype_TEXT: {
            String msg = String((char *)payload, length);
            processWebSocketMessage(RELAY_CLIENT_ID, msg);
            break;
        }
        default:
            break;
        }
    });

    // PRO-347: gate the SSL relay startup on INTERNAL DMA-capable DRAM, not the
    // combined heap. esp_get_free_heap_size() is PSRAM-dominated and almost
    // always passes a <60000 check even when the small internal pool — which the
    // beginSSL() mbedTLS handshake (~50 KB) draws from — is exhausted, yielding
    // `SSL - Memory allocation failed (-32512)`. Mirror the PRO-334 OTA-TLS
    // precedent: refuse when the largest contiguous internal block is below the
    // floor. Only the SSL path is gated; the non-SSL relayWs.begin() path below
    // is unaffected.
    if (useSSL && !sslRelayDramSufficient(gmInternalLargestBlock(), kSslRelayInternalDramFloorBytes)) {
        ESP_LOGW("WebUIPlugin", "Skipping SSL relay: internal DRAM below floor (largest block=%u B < %u B)",
                 static_cast<unsigned>(gmInternalLargestBlock()), static_cast<unsigned>(kSslRelayInternalDramFloorBytes));
        return;
    }

    relayWs.setReconnectInterval(5000);
    if (useSSL) {
        relayWs.beginSSL(host.c_str(), port, path.c_str());
    } else {
        relayWs.begin(host.c_str(), port, path.c_str());
    }

    // relayTaskHandle is guaranteed null here (the live-task case returned above).
    // Relaxed reset is sufficient: the fresh task is published below and
    // xTaskCreatePinnedToCore is itself the synchronization point, so the new
    // task is guaranteed to observe this cleared flag before it runs.
    relayTaskExitRequested.store(false, std::memory_order_relaxed); // fresh task must not see a stale exit request
    // xTaskCreatePinnedToCore wants a raw TaskHandle_t*; create into a local,
    // then publish to the atomic member.
    TaskHandle_t createdHandle = nullptr;
    BaseType_t created = xTaskCreatePinnedToCore(relayLoopTask, "WebUIRelay", 16384, this, 1, &createdHandle, 0);
    if (created != pdPASS) {
        ESP_LOGE("WebUIPlugin", "Failed to create relay task (OOM)");
        relayWs.disconnect();
        return;
    }
    // Release store: published under relayLifecycleMutex before the task can null
    // it; pairs with stopRelay()/startRelay() acquire loads of the handle.
    relayTaskHandle.store(createdHandle, std::memory_order_release);

    relayEnabled = true;
    ESP_LOGI("WebUIPlugin", "Relay client started → %s:%d%s (free heap: %u B)", host.c_str(), port, path.c_str(),
             static_cast<unsigned>(esp_get_free_heap_size()));
    // PRO-352: combined free heap above is misleading for the SSL relay task (PRO-334); also log the
    // internal-DRAM headroom that DMA/TLS handshake allocations actually draw from.
    GM_LOG_INTERNAL_DRAM("relay start");
}

void WebUIPlugin::stopRelay() {
    // Serialized with startRelay() via relayLifecycleMutex (see startRelay()
    // for the cross-task rationale, CAR-259). The lock is held across the bounded
    // ~500 ms spin-wait below; that is acceptable because the wait uses vTaskDelay
    // (yields the CPU) and is strictly bounded.
    SemaphoreGuard lock(relayLifecycleMutex);
    if (!relayEnabled)
        return;
    relayEnabled = false;
    relayConnected = false;
    if (relayTaskHandle.load(std::memory_order_acquire) != nullptr) {
        // Cooperative shutdown (CAR-259): ask the task to tear down its own
        // WebSocket state and self-delete. We must NOT vTaskDelete a remote
        // handle while it may be inside relayWs.loop() (WebSocketsClient /
        // AsyncTCP / mbedTLS allocations), which leaks heap or corrupts the
        // allocator. The task nulls relayTaskHandle just before vTaskDelete(NULL).
        relayTaskExitRequested.store(true, std::memory_order_release);
        // Bound the wait so a wedged task can never hang the caller (this runs
        // on the WiFi-event / AsyncTCP web-server task). Loop cadence is 10 ms;
        // a single relayWs.loop() with an SSL handshake or large frame in flight
        // can exceed that, so allow generous slack before giving up.
        constexpr TickType_t pollInterval = pdMS_TO_TICKS(10);
        constexpr int maxPolls = 50; // ~500 ms
        int polls = 0;
        while (relayTaskHandle.load(std::memory_order_acquire) != nullptr && polls < maxPolls) {
            vTaskDelay(pollInterval);
            ++polls;
        }
        if (relayTaskHandle.load(std::memory_order_acquire) != nullptr) {
            // Task did not exit in time (likely wedged in a long relayWs.loop()).
            // Do NOT vTaskDelete it from here — that is exactly the unsafe
            // primitive we are avoiding. Leave relayTaskExitRequested = true so
            // the task self-deletes the moment its in-flight relayWs.loop()
            // returns: relayLoopTask checks the flag UNCONDITIONALLY at the top
            // of every iteration (before the relayEnabled guard), so even with
            // relayEnabled now false it will run its own relayWs.disconnect()
            // and vTaskDelete(NULL) rather than idling forever with its socket /
            // mbedTLS allocations un-freed. A later startRelay() waits for the
            // handle to clear before creating a fresh task (CAR-259).
            ESP_LOGW("WebUIPlugin", "Relay task did not exit within %d ms; it will self-delete when its loop returns",
                     maxPolls * 10);
            return;
        }
    }
    // Task is gone (or never existed). Clear the request flag for the next start.
    // Relaxed: no relay task is running on this path (handle observed null above),
    // so there is nothing to synchronize with.
    relayTaskExitRequested.store(false, std::memory_order_relaxed);
}

void WebUIPlugin::broadcastAll(const String &msg) {
    // PRO-313: textAll() walks the client list; serialize against the AsyncTCP
    // task's connect/disconnect mutation. The lock is released before
    // broadcastRelayMsg() (which takes relayMutex) so the two locks never nest.
    //
    // PRO-357: holding wsMutex across ws.textAll() is only safe because the WS
    // client has setCloseClientOnQueueFull DISABLED (see WS_EVT_CONNECT). With it
    // enabled, a client whose TX queue is full during this textAll() walk would be
    // closed INLINE by the library, synchronously firing WS_EVT_DISCONNECT on this
    // same (loop) task while wsMutex is held — and that handler re-takes the
    // non-recursive wsMutex -> self-deadlock -> AsyncTCP starvation -> Task
    // Watchdog reboot. Disabling the inline close (frames are dropped on a
    // hard-capped queue instead) removes the re-entrancy, so the textAll() send
    // can stay inside the wsMutex critical section that PRO-313 requires for the
    // list walk. We deliberately do NOT try to "snapshot clients under the lock,
    // send outside it" here: with ESPAsyncWebServer v3.9.1 a per-client send
    // outside the lock either re-walks _clients unserialized (re-opening the
    // PRO-313 corruption) or holds raw AsyncWebSocketClient* across the unlocked
    // window where the AsyncTCP task can erase+free that std::list node
    // (use-after-free). Removing the inline close at the source is the safe fix.
    {
        SemaphoreGuard lock(wsMutex);
        ws.textAll(msg);
    }
    broadcastRelayMsg(msg);
}

void WebUIPlugin::broadcastRelayMsg(const String &msg) {
    if (!relayEnabled || relayMutex == nullptr)
        return;
    if (xSemaphoreTake(relayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (relayOutBuffer.size() < 64) {
            relayOutBuffer.push_back(msg);
        } else {
            ESP_LOGW("WebUIPlugin", "Relay out buffer full, dropping message");
        }
        xSemaphoreGive(relayMutex);
    }
}

void WebUIPlugin::sendResponse(uint32_t clientId, JsonDocument &response) {
    String responseStr;
    serializeJson(response, responseStr);

    if (clientId != RELAY_CLIENT_ID) {
        size_t bufferSize = measureJson(response);
        auto *buffer = ws.makeBuffer(bufferSize);
        if (buffer) {
            serializeJson(response, buffer->get(), bufferSize);
            // PRO-313: text(id, ...) resolves the client by walking the client
            // list, which races with loopTask's textAll()/cleanupClients(). The
            // makeBuffer()/serializeJson() above touch no client list, so only
            // the send is serialized. Lock released before broadcastRelayMsg()
            // (relayMutex) so the two locks never nest.
            SemaphoreGuard lock(wsMutex);
            ws.text(clientId, buffer);
        }
    }
    // Always forward responses to relay so remote browsers receive them
    broadcastRelayMsg(responseStr);
}

void WebUIPlugin::processWebSocketMessage(uint32_t clientId, const String &msg) {
    ESP_LOGV("WebUIPlugin", "Processing message from %s: %.*s", clientId == RELAY_CLIENT_ID ? "relay" : "local",
             (int)msg.length(), msg.c_str());
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg.c_str());
    if (err)
        return;

    String msgType = doc["tp"].as<String>();
    if (msgType.startsWith("req:profiles:")) {
        handleProfileRequest(clientId, doc);
    } else if (msgType.startsWith("req:beans:") && msgType != "req:beans:select") {
        handleBeanRequest(clientId, doc);
    } else if (msgType.startsWith("req:grinders:")) {
        handleGrinderRequest(clientId, doc);
    } else if (msgType == "req:ota-settings") {
        handleOTASettings(clientId, doc);
    } else if (msgType == "req:ota-start") {
        handleOTAStart(clientId, doc);
    } else if (msgType == "req:autotune-start") {
        handleAutotuneStart(clientId, doc);
    } else if (msgType == "req:process:activate") {
        controller->activate();
    } else if (msgType == "req:process:deactivate") {
        controller->deactivate();
        controller->clear();
    } else if (msgType == "req:process:clear") {
        controller->clear();
    } else if (msgType == "req:grind:activate") {
        controller->activateGrind();
    } else if (msgType == "req:grind:deactivate") {
        controller->deactivateGrind();
    } else if (msgType == "req:change-grind-target") {
        // 0/1 toggle: selects Time vs Weight mode for the grind target.
        // (The grams value itself is set via req:raise/lower-grind-target.)
        if (doc["target"].is<uint8_t>()) {
            controller->getSettings().setVolumetricTarget(doc["target"].as<uint8_t>());
        } else {
            ESP_LOGW("WebUIPlugin", "req:change-grind-target ignored: missing or invalid 'target'");
        }
    } else if (msgType == "req:raise-temp") {
        controller->raiseTemp();
    } else if (msgType == "req:lower-temp") {
        controller->lowerTemp();
    } else if (msgType == "req:raise-grind-target") {
        controller->raiseGrindTarget();
    } else if (msgType == "req:lower-grind-target") {
        controller->lowerGrindTarget();
    } else if (msgType == "req:raise-brew-target") {
        controller->raiseBrewTarget();
    } else if (msgType == "req:lower-brew-target") {
        controller->lowerBrewTarget();
    } else if (msgType == "req:manual:update") {
        if (controller->getMode() != MODE_MANUAL || !controller->isManualAvailable())
            return;
        int targetType = controller->getManualTargetType();
        if (doc["targetType"].is<const char *>()) {
            String requestedType = doc["targetType"].as<const char *>();
            targetType = requestedType == "flow" ? MANUAL_TARGET_FLOW : MANUAL_TARGET_PRESSURE;
        }
        JsonVariantConst pressureValue = doc["pressure"];
        JsonVariantConst flowValue = doc["flow"];
        JsonVariantConst temperatureValue = doc["temperature"];
        float pressure =
            pressureValue.is<float>() || pressureValue.is<int>() ? pressureValue.as<float>() : controller->getManualPressure();
        float flow = flowValue.is<float>() || flowValue.is<int>() ? flowValue.as<float>() : controller->getManualFlow();
        int temperature = temperatureValue.is<int>() || temperatureValue.is<float>() ? temperatureValue.as<int>()
                                                                                     : controller->getManualTemperature();
        controller->updateManualTargets(targetType, pressure, flow, temperature);
    } else if (msgType == "req:change-mode") {
        if (doc["mode"].is<uint8_t>()) {
            uint8_t newMode = doc["mode"].as<uint8_t>();
            if (newMode == MODE_GRIND && !controller->isGrindAvailable())
                return;
            if (newMode == MODE_MANUAL && !controller->isManualAvailable())
                return;
            // PRO-261: honor the post-shot extended-recording / scale-settle gate
            // that the display's auto-steam path already respects (DefaultUI::loop
            // / pendingAutoSteam, PRO-223 / PRO-248 / PRO-232). This handler runs
            // on the AsyncTCP / relay task, NOT the main loop task. deactivate()
            // ends the active process and synchronously fires controller:brew:end,
            // which opens the settle window (ShotHistory.endRecording ->
            // extendedRecording) iff a healthy BLE scale was the volumetric source.
            controller->deactivate();
            // If the settle window is open, defer clear()+setMode() to loop() (main
            // task) so the BLE scale stays connected, record() keeps logging, and
            // the final drips reach the recorded yield. Calling clear() now would
            // fire controller:brew:clear -> endExtendedRecording(), aborting exactly
            // the window PRO-223/PRO-248 built. setMode() now would stop record().
            // A redundant/duplicate request while a window is already in flight just
            // re-posts the same target without collapsing it (no clear() runs).
            // PRO-265: STANDBY is an explicit user stop and must NEVER defer — it
            // bypasses the settle window entirely and stops immediately, mirroring
            // Controller::activateStandby() and the physical STANDBY button (which
            // are not gated). Only non-standby targets (auto-steam MODE_STEAM, grind,
            // manual) keep PRO-261's settle behavior.
            if (shouldDeferModeChange(newMode, ShotHistory.isExtendedRecording())) {
                // Latch target before raising the flag so loop() never reads a stale
                // target for a freshly-armed deferral (volatile handoff, see header).
                pendingModeChangeTarget = newMode;
                pendingModeChange = true;
            } else {
                // No settle window (no scale / flow-estimation / time-based shot, or
                // not coming from an active brew), OR an explicit STANDBY request —
                // engage the new mode immediately, no added latency. Setting
                // pendingModeChange = false here also disarms any prior deferral
                // (e.g. an auto-steam defer armed moments earlier), so a mid-window
                // standby cancels the pending transition instead of being shadowed.
                pendingModeChange = false;
                controller->clear();
                controller->setMode(newMode);
            }
        }
    } else if (msgType == "req:change-brew-target") {
        // Brew target is a grams value (yield) from the Home dashboard YIELD
        // slider. The previous handler cast it to uint8_t and routed to
        // Settings::setVolumetricTarget(bool) — the volumetric MODE toggle —
        // so the dashboard yield silently never reached the active profile.
        // Accept float|int|uint, route to Controller::setBrewTarget which
        // mutates the in-memory profile's volumetric target. CAR-252.
        if (doc["target"].is<float>()) {
            controller->setBrewTarget(doc["target"].as<float>());
        } else if (doc["target"].is<int>()) {
            controller->setBrewTarget(static_cast<float>(doc["target"].as<int>()));
        } else if (doc["target"].is<uint8_t>()) {
            controller->setBrewTarget(static_cast<float>(doc["target"].as<uint8_t>()));
        } else {
            ESP_LOGW("WebUIPlugin", "req:change-brew-target ignored: missing or invalid 'target'");
        }
    } else if (msgType == "req:autosteam:set") {
        // Device-authoritative auto-steam toggle (PRO-225). Persisted via
        // Settings and rebroadcast to all clients in the next evt:status as "as".
        JsonVariantConst enabledValue = doc["enabled"];
        if (enabledValue.is<bool>()) {
            controller->getSettings().setAutoSteamEnabled(enabledValue.as<bool>());
        } else if (enabledValue.is<int>()) {
            controller->getSettings().setAutoSteamEnabled(enabledValue.as<int>() != 0);
        } else {
            ESP_LOGW("WebUIPlugin", "req:autosteam:set ignored: missing or invalid 'enabled'");
        }
    } else if (msgType == "req:dose:set") {
        // Device-authoritative brew dose in grams (PRO-225). Validated and
        // clamped by Settings::setDoseGrams, rebroadcast as "dg" in evt:status.
        JsonVariantConst gramsValue = doc["grams"];
        // ArduinoJson treats every JSON number (integer or float) as is<float>()==true,
        // while strings/bools/null are false, so this single test accepts any numeric value.
        if (!gramsValue.isNull() && gramsValue.is<float>()) {
            const double grams = gramsValue.as<double>();
            // Reject non-finite values (NaN, +/-inf are possible as<float>() outcomes) and out-of-range doses.
            if (!std::isfinite(grams) || grams <= 0.0 || grams > 200.0) {
                ESP_LOGW("WebUIPlugin", "req:dose:set ignored: 'grams' out of range");
            } else {
                controller->getSettings().setDoseGrams(grams);
            }
        } else {
            ESP_LOGW("WebUIPlugin", "req:dose:set ignored: missing or invalid 'grams'");
        }
    } else if (msgType == "req:beans:select") {
        String beanName = doc["name"].is<String>() ? doc["name"].as<String>() : String("");
        controller->getSettings().setSelectedBean(beanName);
        pluginManager->trigger("beans:selected", "name", beanName);
    } else if (msgType == "req:history:rebuild") {
        JsonDocument resp;
        resp["tp"] = "res:history:rebuild";
        if (doc["rid"].is<const char *>())
            resp["rid"] = doc["rid"];
        resp["msg"] = "Rebuild started";
        sendResponse(clientId, resp);
        ShotHistory.startAsyncRebuild();
    } else if (msgType.startsWith("req:history")) {
        JsonDocument resp;
        ShotHistory.handleRequest(doc, resp);
        sendResponse(clientId, resp);
    } else if (msgType == "req:flush:start") {
        handleFlushStart(clientId, doc);
    }
}

void WebUIPlugin::handleWebSocketData(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                                      uint8_t *data, size_t len) {

    auto *info = static_cast<AwsFrameInfo *>(arg);
    const uint32_t cid = client->id();

    if (info->index == 0) {
        // PRO-358: log internal DMA-capable DRAM at the start of a reassembly so
        // the on-hardware headroom delta from routing this buffer to PSRAM is
        // serial-measurable. With rxBuffers backed by PsramAllocator the growth
        // below draws from PSRAM, not the internal pool, so this figure should
        // stay flat across large control messages (contrast the pre-PRO-358
        // std::string, which pinned internal DRAM for the whole message).
        GM_LOG_INTERNAL_DRAM("ws-reassembly:begin");
        auto &buf = rxBuffers[cid];
        buf.clear();
        if (info->len <= 64 * 1024) {
            buf.reserve(info->len);
        }
    }

    auto &buf = rxBuffers[cid];

    // PRO-350: bound the reassembled total. The 64 KiB check above gates only
    // the optimistic reserve(); the append() below is what actually grows the
    // buffer. Without this guard a client declaring a huge info->len, or
    // streaming many continuation fragments, grows rxBuffers[cid] without bound
    // until allocation fails (bad_alloc / abort). Refuse on exceed: drop the
    // buffer, close the client with a protocol-error code, and stop accumulating
    // — never append past the cap. (Real-display analog of the sim PRO-209
    // single-frame cap; here the cap bounds the cumulative reassembled total.)
    if (wsReassemblyWouldExceed(buf.size(), len, info->len)) {
        buf.clear();
        rxBuffers.erase(cid);
        client->close(1009); // 1009 = message too big
        return;
    }

    buf.append(reinterpret_cast<const char *>(data), len);
    const bool isFinal = info->final && (info->index + len) == info->len;

    if (isFinal) {
        if (info->opcode == WS_TEXT) {
            processWebSocketMessage(cid, String(buf.c_str(), buf.size()));
        }
        rxBuffers.erase(cid);
        // PRO-358: after releasing the (PSRAM-backed) reassembly buffer, log the
        // internal-DRAM figure again. Paired with ws-reassembly:begin this makes
        // the reclaimed-internal-DRAM delta visible on the device serial.
        GM_LOG_INTERNAL_DRAM("ws-reassembly:done");
    }
}

// Resolve a stored OTA channel string to the GitHub release URL fragment.
// "latest"      -> "latest" (resolves to most recent non-prerelease)
// "beta"        -> "tag/beta" (moving tag tracking the master branch)
// "nightly"     -> "tag/nightly"
// "tag:<semver>" (validated against STABLE_VERSIONS allow-list) -> "tag/<semver>"
// anything else -> "latest"
static String resolveReleaseUrl(const String &channel) {
    if (channel == "beta") {
        return RELEASE_URL + "tag/beta";
    }
    if (channel == "nightly") {
        return RELEASE_URL + "tag/nightly";
    }
    if (channel.startsWith("tag:")) {
        const String tag = channel.substring(4);
        for (size_t i = 0; i < STABLE_VERSIONS_COUNT; ++i) {
            if (tag == STABLE_VERSIONS[i]) {
                return RELEASE_URL + "tag/" + tag;
            }
        }
    }
    return RELEASE_URL + "latest";
}

// Normalize an incoming channel to the value we persist in settings.
// "beta" and "nightly" are accepted moving-tag channels; "tag:<semver>" is
// validated against the STABLE_VERSIONS allow-list. Unknown values fall back
// to "latest" so a malformed websocket payload can never poison the stored
// setting.
static String normalizeChannel(const String &channel) {
    if (channel == "beta")
        return "beta";
    if (channel == "nightly")
        return "nightly";
    if (channel.startsWith("tag:")) {
        const String tag = channel.substring(4);
        for (size_t i = 0; i < STABLE_VERSIONS_COUNT; ++i) {
            if (tag == STABLE_VERSIONS[i]) {
                return channel;
            }
        }
    }
    return "latest";
}

void WebUIPlugin::handleOTASettings(uint32_t clientId, JsonDocument &request) {
    // `lastUpdateCheck` is intentionally exempt from the loop-task-ownership model
    // the rest of this handler follows (CAR-178/CAR-377): it is a single
    // word-aligned `unsigned long` whose write is atomic on ESP32, and 0 is a
    // force-recheck sentinel where a stale read merely delays the next check by one
    // interval. So it is safe to set directly here rather than via a deferred flag.
    lastUpdateCheck = 0;
    // PRO-345: same single-atomic-word / force-recheck-sentinel reasoning applies —
    // clear the defer-notice throttle so a forced recheck that lands on Defer
    // surfaces the truthful "deferred" status immediately rather than after one interval.
    lastOtaDeferNotice = 0;
    // This handler runs on the AsyncTCP web-server task (local WS clients) or the
    // relay task (remote clients) — NOT the loop task. `ota` is single-threaded
    // and owned by the loop task (CAR-178), so we must not call into it here.
    // Instead, post the release-URL change and a status-refresh request as
    // deferred intent; loop() drains both on the loop task. This also means the
    // handler returns immediately and never blocks a WS client behind a
    // multi-minute ota->update() in progress.
    if (request["update"].as<bool>()) {
        if (!request["channel"].isNull()) {
            const String normalized = normalizeChannel(request["channel"].as<String>());
            controller->getSettings().setOTAChannel(normalized);
            const String url = resolveReleaseUrl(normalized);
            if (otaIntentMutex != nullptr && xSemaphoreTake(otaIntentMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pendingReleaseUrl = url;
                pendingReleaseUrlChange = true;
                xSemaphoreGive(otaIntentMutex);
            } else {
                // Should be effectively impossible — the lock is only ever held
                // for three trivial assignments on the loop-task drain side — but
                // never drop a channel change silently. Raise the flag anyway: the
                // loop-task drain re-resolves the URL from the persisted channel
                // when no explicit URL was handed off (emptyHandoff), so the new
                // channel still reaches `ota` on the next loop iteration.
                pendingReleaseUrlChange = true;
                ESP_LOGW("WebUIPlugin", "OTA release-URL handoff contended; channel persisted, loop will re-resolve");
            }
        }
    }
    // Defer the status broadcast (which reads ota->getCurrentVersion() /
    // isUpdateAvailable()) onto the loop task as well.
    pendingOtaStatusPush = true;
}

void WebUIPlugin::handleOTAStart(uint32_t clientId, JsonDocument &request) {
    // Runs on the AsyncTCP / relay task, NOT the loop task. `updating` and the
    // non-atomic `updateComponent` String are loop-task-owned (CAR-377), so post
    // the intent here and let loop() latch it on the loop task. The handler returns
    // immediately; the update starts on the next loop iteration.
    //
    // Single-in-flight semantics: there is one intent slot. Two ota-start requests
    // arriving before loop() latches coalesce last-writer-wins (the later component
    // overwrites the earlier) — exactly one update still runs. That is intended;
    // concurrent ota-start requests are not a supported workflow.
    const String component = request["cp"].is<String>() ? request["cp"].as<String>() : String("");
    if (otaIntentMutex != nullptr && xSemaphoreTake(otaIntentMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        pendingUpdateComponent = component;
        pendingOtaStart = true;
        xSemaphoreGive(otaIntentMutex);
    } else {
        // Effectively impossible (the lock only ever wraps a few trivial
        // assignments), but never drop a start request silently. Raise the flag
        // anyway; loop() finds an empty pendingUpdateComponent and defaults to a
        // full update (both display and controller) — the safe superset.
        pendingOtaStart = true;
        ESP_LOGW("WebUIPlugin", "OTA-start handoff contended; defaulting to full update");
    }
}

void WebUIPlugin::handleAutotuneStart(uint32_t clientId, JsonDocument &request) {
    int testTime = request["time"].as<int>();
    int samples = request["samples"].as<int>();
    controller->autotune(testTime, samples);
}

void WebUIPlugin::handleProfileRequest(uint32_t clientId, JsonDocument &request) {
    JsonDocument response;
    auto type = request["tp"].as<String>();
    ESP_LOGI("WebUIPlugin", "Handling request: %s", type.c_str());
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    if (type == "req:profiles:list") {
        auto arr = response["profiles"].to<JsonArray>();
        for (auto const &id : profileManager->listProfiles()) {
            Profile profile{};
            profileManager->loadProfile(id, profile);
            auto p = arr.add<JsonObject>();
            writeProfile(p, profile);
        }
    } else if (type == "req:profiles:load") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid profile id");
            sendResponse(clientId, response);
            return;
        }
        // PRO-334: log internal-DRAM headroom right before the SD-backed profile
        // read that runs on this (AsyncTCP) task — the read that used to wedge
        // the task into a WDT reboot when the internal pool was exhausted.
        GM_LOG_INTERNAL_DRAM("before req:profiles:load SD read");
        Profile profile;
        if (profileManager->loadProfile(id, profile)) {
            auto obj = response["profile"].to<JsonObject>();
            writeProfile(obj, profile);
        } else {
            response["error"] = F("Profile not found");
        }
    } else if (type == "req:profiles:save") {
        auto obj = request["profile"].as<JsonObject>();
        Profile profile;
        if (!parseProfile(obj, profile)) {
            response["error"] = F("Invalid profile");
            sendResponse(clientId, response);
            return;
        }
        if (!profileManager->saveProfile(profile)) {
            response["error"] = F("Save failed");
        }
        auto respObj = response["profile"].to<JsonObject>();
        writeProfile(respObj, profile);
    } else if (type == "req:profiles:delete") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid profile id");
            sendResponse(clientId, response);
            return;
        }
        if (!profileManager->deleteProfile(id)) {
            response["error"] = F("Delete failed");
        }
    } else if (type == "req:profiles:select") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid profile id");
            sendResponse(clientId, response);
            return;
        }
        profileManager->selectProfile(id);
    } else if (type == "req:profiles:favorite") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid profile id");
            sendResponse(clientId, response);
            return;
        }
        profileManager->addFavoritedProfile(id);
    } else if (type == "req:profiles:unfavorite") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid profile id");
            sendResponse(clientId, response);
            return;
        }
        profileManager->removeFavoritedProfile(id);
    } else if (type == "req:profiles:reorder") {
        // Expect an array of profile IDs in desired order
        if (request["order"].is<JsonArray>()) {
            std::vector<String> order;
            for (JsonVariant v : request["order"].as<JsonArray>()) {
                if (v.is<String>()) {
                    String id = v.as<String>();
                    if (isSafeId(id) && std::find(order.begin(), order.end(), id) == order.end()) {
                        order.emplace_back(std::move(id));
                    }
                }
            }
            controller->getSettings().setProfileOrder(order);
        }
    }

    sendResponse(clientId, response);
}

void WebUIPlugin::handleBeanRequest(uint32_t clientId, JsonDocument &request) {
    JsonDocument response;
    auto type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    if (type == "req:beans:list") {
        auto arr = response["beans"].to<JsonArray>();
        for (const auto &bean : beanManager->listBeans()) {
            auto obj = arr.add<JsonObject>();
            writeBean(obj, bean);
        }
    } else if (type == "req:beans:load") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid bean id");
            sendResponse(clientId, response);
            return;
        }
        if (auto bean = beanManager->loadBean(id)) {
            auto obj = response["bean"].to<JsonObject>();
            writeBean(obj, *bean);
        } else {
            response["error"] = F("Load failed");
        }
    } else if (type == "req:beans:save") {
        BeanEntry bean{};
        if (!parseBean(request["bean"].as<JsonObject>(), bean) || !beanManager->saveBean(bean)) {
            response["error"] = F("Save failed");
        } else {
            auto obj = response["bean"].to<JsonObject>();
            writeBean(obj, bean);
        }
    } else if (type == "req:beans:delete") {
        auto id = request["id"].as<String>();
        if (!isSafeId(id)) {
            response["error"] = F("Invalid bean id");
            sendResponse(clientId, response);
            return;
        }
        if (auto bean = beanManager->loadBean(id); bean && controller->getSettings().getSelectedBean() == bean->name) {
            controller->getSettings().setSelectedBean("");
            pluginManager->trigger("beans:selected", "name", "");
        }
        if (!beanManager->deleteBean(id)) {
            response["error"] = F("Delete failed");
        }
    }

    sendResponse(clientId, response);
}

void WebUIPlugin::handleGrinderRequest(uint32_t clientId, JsonDocument &request) {
    JsonDocument response;
    auto type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    if (type == "req:grinders:list") {
        auto arr = response["grinders"].to<JsonArray>();
        for (const auto &name : grinderManager->listGrinders()) {
            arr.add(name);
        }
    } else if (type == "req:grinders:save") {
        // Accept either a single `name` (back-compat) or a `names` array
        // (batch sync from the web client). The device performs the
        // merge/dedup/cap authoritatively and returns the canonical list, so
        // the client never has to model eviction.
        bool ok = false;
        if (request["names"].is<JsonArray>()) {
            std::vector<String> names;
            for (JsonVariant v : request["names"].as<JsonArray>()) {
                names.push_back(v.as<String>());
            }
            ok = grinderManager->recordGrinders(names);
        } else {
            ok = grinderManager->recordGrinder(request["name"].as<String>());
        }
        if (!ok) {
            response["error"] = F("Save failed");
        } else {
            auto arr = response["grinders"].to<JsonArray>();
            for (const auto &grinder : grinderManager->listGrinders()) {
                arr.add(grinder);
            }
        }
    }

    sendResponse(clientId, response);
}

void WebUIPlugin::handleSettings(AsyncWebServerRequest *request) {
    if (request->method() == HTTP_POST) {
        controller->getSettings().batchUpdate([request](Settings *settings) {
            if (request->hasArg("startupMode"))
                settings->setStartupMode(request->arg("startupMode") == "brew" ? MODE_BREW : MODE_STANDBY);
            if (request->hasArg("targetSteamTemp"))
                settings->setTargetSteamTemp(request->arg("targetSteamTemp").toInt());
            if (request->hasArg("targetWaterTemp"))
                settings->setTargetWaterTemp(request->arg("targetWaterTemp").toInt());
            if (request->hasArg("temperatureOffset"))
                settings->setTemperatureOffset(request->arg("temperatureOffset").toInt());
            if (request->hasArg("pressureScaling"))
                settings->setPressureScaling(request->arg("pressureScaling").toFloat());
            if (request->hasArg("pid"))
                settings->setPid(request->arg("pid"));
            if (request->hasArg("pumpModelCoeffs"))
                settings->setPumpModelCoeffs(request->arg("pumpModelCoeffs"));
            if (request->hasArg("wifiSsid"))
                settings->setWifiSsid(request->arg("wifiSsid"));
            if (request->hasArg("mdnsName"))
                settings->setMdnsName(request->arg("mdnsName"));
            if (request->hasArg("wifiPassword") && request->arg("wifiPassword") != kSecretSentinel)
                settings->setWifiPassword(request->arg("wifiPassword"));
            settings->setHomekit(request->hasArg("homekit"));
            settings->setBoilerFillActive(request->hasArg("boilerFillActive"));
            if (request->hasArg("startupFillTime"))
                settings->setStartupFillTime(request->arg("startupFillTime").toInt() * 1000);
            if (request->hasArg("steamFillTime"))
                settings->setSteamFillTime(request->arg("steamFillTime").toInt() * 1000);
            settings->setSmartGrindActive(request->hasArg("smartGrindActive"));
            // PRO-266: diagnostic UDP log tee, default OFF. Checkbox semantics —
            // present means enabled. PRO-271: takes effect immediately while
            // online — the "settings:changed" trigger below arms the tee without
            // a reboot (DiagnosticLogPlugin::tryInstall, also driven from loop()).
            settings->setDiagnosticLogEnabled(request->hasArg("diagnosticLog"));
            if (request->hasArg("smartGrindIp"))
                settings->setSmartGrindIp(request->arg("smartGrindIp"));
            if (request->hasArg("smartGrindMode"))
                settings->setSmartGrindMode(request->arg("smartGrindMode").toInt());
            settings->setHomeAssistant(request->hasArg("homeAssistant"));
            if (request->hasArg("haUser"))
                settings->setHomeAssistantUser(request->arg("haUser"));
            if (request->hasArg("haPassword") && request->arg("haPassword") != kSecretSentinel)
                settings->setHomeAssistantPassword(request->arg("haPassword"));
            if (request->hasArg("haIP"))
                settings->setHomeAssistantIP(request->arg("haIP"));
            if (request->hasArg("haPort"))
                settings->setHomeAssistantPort(request->arg("haPort").toInt());
            if (request->hasArg("haTopic"))
                settings->setHomeAssistantTopic(request->arg("haTopic"));
            settings->setMomentaryButtons(request->hasArg("momentaryButtons"));
            settings->setAllowYieldOverride(request->hasArg("allowYieldOverride"));
            settings->setDelayAdjust(request->hasArg("delayAdjust"));
            if (request->hasArg("brewDelay"))
                settings->setBrewDelay(request->arg("brewDelay").toDouble());
            if (request->hasArg("grindDelay"))
                settings->setGrindDelay(request->arg("grindDelay").toDouble());
            if (request->hasArg("timezone"))
                settings->setTimezone(request->arg("timezone"));
            settings->setClockFormat(request->hasArg("clock24hFormat"));
            if (request->hasArg("standbyTimeout"))
                settings->setStandbyTimeout(request->arg("standbyTimeout").toInt() * 1000);
            if (request->hasArg("mainBrightness"))
                settings->setMainBrightness(request->arg("mainBrightness").toInt());
            if (request->hasArg("standbyBrightness"))
                settings->setStandbyBrightness(request->arg("standbyBrightness").toInt());
            if (request->hasArg("standbyBrightnessTimeout"))
                settings->setStandbyBrightnessTimeout(request->arg("standbyBrightnessTimeout").toInt() * 1000);
            if (request->hasArg("steamPumpPercentage"))
                settings->setSteamPumpPercentage(request->arg("steamPumpPercentage").toFloat());
            if (request->hasArg("steamPumpCutoff"))
                settings->setSteamPumpCutoff(request->arg("steamPumpCutoff").toFloat());
            if (request->hasArg("themeMode"))
                settings->setThemeMode(request->arg("themeMode").toInt());
            if (request->hasArg("sunriseR"))
                settings->setSunriseR(request->arg("sunriseR").toInt());
            if (request->hasArg("sunriseG"))
                settings->setSunriseG(request->arg("sunriseG").toInt());
            if (request->hasArg("sunriseB"))
                settings->setSunriseB(request->arg("sunriseB").toInt());
            if (request->hasArg("sunriseW"))
                settings->setSunriseW(request->arg("sunriseW").toInt());
            if (request->hasArg("sunriseExtBrightness"))
                settings->setSunriseExtBrightness(request->arg("sunriseExtBrightness").toInt());
            if (request->hasArg("emptyTankDistance"))
                settings->setEmptyTankDistance(request->arg("emptyTankDistance").toInt());
            if (request->hasArg("fullTankDistance"))
                settings->setFullTankDistance(request->arg("fullTankDistance").toInt());
            if (request->hasArg("altRelayFunction"))
                settings->setAltRelayFunction(request->arg("altRelayFunction").toInt());
            settings->setAutoWakeupEnabled(request->hasArg("autowakeupEnabled") &&
                                           request->arg("autowakeupEnabled").length() > 0);
            if (request->hasArg("autowakeupSchedules")) {
                // Handle schedule format with days
                String schedulesStr = request->arg("autowakeupSchedules");
                std::vector<AutoWakeupSchedule> schedules;

                if (schedulesStr.length() > 0) {
                    // Split semicolon-separated schedules
                    int start = 0;
                    int end = schedulesStr.indexOf(';');

                    while (end != -1 || start < schedulesStr.length()) {
                        String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

                        int pipePos = scheduleStr.indexOf('|');
                        if (pipePos != -1) {
                            String timeStr = scheduleStr.substring(0, pipePos);
                            String daysStr = scheduleStr.substring(pipePos + 1);

                            AutoWakeupSchedule schedule;
                            schedule.time = timeStr;

                            if (daysStr.length() == 7) {
                                for (int i = 0; i < 7; i++) {
                                    schedule.days[i] = (daysStr.charAt(i) == '1');
                                }
                            }

                            schedules.push_back(schedule);
                        }

                        if (end == -1)
                            break;
                        start = end + 1;
                        end = schedulesStr.indexOf(';', start);
                    }
                }

                if (schedules.empty()) {
                    schedules.push_back(AutoWakeupSchedule("07:00")); // Default fallback
                }
                settings->setAutoWakeupSchedules(schedules);
            }
            if (request->hasArg("flushDuration"))
                settings->setFlushDuration(request->arg("flushDuration").toInt() * 1000);
            if (request->hasArg("cloudRelayUrl"))
                settings->setCloudRelayUrl(request->arg("cloudRelayUrl"));
            if (request->hasArg("cloudRelayToken") && request->arg("cloudRelayToken") != kSecretSentinel)
                settings->setCloudRelayToken(request->arg("cloudRelayToken"));
            if (request->hasArg("cloudRelayEnabled"))
                settings->setCloudRelayEnabled(request->arg("cloudRelayEnabled") == "1");
            settings->save(true);
        });
        pluginManager->trigger("settings:changed");
        controller->setTargetTemp(controller->getTargetTemp());
        controller->setPumpModelCoeffs();
        if (request->hasArg("cloudRelayUrl") || request->hasArg("cloudRelayToken") || request->hasArg("cloudRelayEnabled")) {
            stopRelay();
            startRelay();
        }
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    JsonDocument doc;
    Settings const &settings = controller->getSettings();
    doc["startupMode"] = settings.getStartupMode() == MODE_BREW ? "brew" : "standby";
    doc["targetSteamTemp"] = settings.getTargetSteamTemp();
    doc["targetWaterTemp"] = settings.getTargetWaterTemp();
    doc["homekit"] = settings.isHomekit();
    doc["homeAssistant"] = settings.isHomeAssistant();
    doc["haUser"] = settings.getHomeAssistantUser();
    doc["haPassword"] = kSecretSentinel;
    doc["haIP"] = settings.getHomeAssistantIP();
    doc["haPort"] = settings.getHomeAssistantPort();
    doc["haTopic"] = settings.getHomeAssistantTopic();
    doc["pid"] = settings.getPid();
    doc["pumpModelCoeffs"] = settings.getPumpModelCoeffs();
    doc["wifiSsid"] = settings.getWifiSsid();
    // Always mask: never return the plaintext WiFi password to clients. The
    // previous AP-mode-only mask leaked it over HTTP whenever the device was
    // on the user's Wi-Fi.
    doc["wifiPassword"] = kSecretSentinel;
    doc["mdnsName"] = settings.getMdnsName();
    doc["temperatureOffset"] = String(settings.getTemperatureOffset());
    doc["pressureScaling"] = String(settings.getPressureScaling());
    doc["boilerFillActive"] = settings.isBoilerFillActive();
    doc["startupFillTime"] = settings.getStartupFillTime() / 1000;
    doc["steamFillTime"] = settings.getSteamFillTime() / 1000;
    doc["smartGrindActive"] = settings.isSmartGrindActive();
    doc["diagnosticLog"] = settings.getDiagnosticLogEnabled(); // PRO-266
    doc["smartGrindIp"] = settings.getSmartGrindIp();
    doc["smartGrindMode"] = settings.getSmartGrindMode();
    doc["momentaryButtons"] = settings.isMomentaryButtons();
    doc["allowYieldOverride"] = settings.isAllowYieldOverride();
    doc["brewDelay"] = settings.getBrewDelay();
    doc["grindDelay"] = settings.getGrindDelay();
    doc["delayAdjust"] = settings.isDelayAdjust();
    doc["timezone"] = settings.getTimezone();
    doc["clock24hFormat"] = settings.isClock24hFormat();
    doc["standbyTimeout"] = settings.getStandbyTimeout() / 1000;
    doc["mainBrightness"] = settings.getMainBrightness();
    doc["standbyBrightness"] = settings.getStandbyBrightness();
    doc["standbyBrightnessTimeout"] = settings.getStandbyBrightnessTimeout() / 1000;
    doc["steamPumpPercentage"] = settings.getSteamPumpPercentage();
    doc["steamPumpCutoff"] = settings.getSteamPumpCutoff();
    doc["themeMode"] = settings.getThemeMode();
    doc["sunriseR"] = settings.getSunriseR();
    doc["sunriseG"] = settings.getSunriseG();
    doc["sunriseB"] = settings.getSunriseB();
    doc["sunriseW"] = settings.getSunriseW();
    doc["sunriseExtBrightness"] = settings.getSunriseExtBrightness();
    doc["emptyTankDistance"] = settings.getEmptyTankDistance();
    doc["fullTankDistance"] = settings.getFullTankDistance();
    doc["altRelayFunction"] = settings.getAltRelayFunction();
    // Add auto-wakeup settings to response
    doc["autowakeupEnabled"] = settings.isAutoWakeupEnabled();
    doc["flushDuration"] = settings.getFlushDuration() / 1000;
    doc["cloudRelayUrl"] = settings.getCloudRelayUrl();
    doc["cloudRelayToken"] = kSecretSentinel;
    doc["cloudRelayEnabled"] = settings.isCloudRelayEnabled();

    // Add schedule format with days
    std::vector<AutoWakeupSchedule> autowakeupSchedules = settings.getAutoWakeupSchedules();
    String schedulesStr = "";
    for (size_t i = 0; i < autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesStr += ";";
        schedulesStr += autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesStr += autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    doc["autowakeupSchedules"] = schedulesStr;
    serializeJson(doc, *response);
    request->send(response);

    if (request->method() == HTTP_POST && request->hasArg("restart"))
        ESP.restart();
}

#if GAGGIMATE_ENABLE_BLE_SCALE
void WebUIPlugin::handleBLEScaleList(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray scalesArray = doc.to<JsonArray>();
    for (const DiscoveredDevice &device : BLEScales.getDiscoveredScales()) {
        JsonDocument scale;
        scale["uuid"] = device.getAddress().toString();
        scale["name"] = device.getName();
        scale["rssi"] = device.getRSSI();
        scalesArray.add(scale);
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleScan(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        request->send(404);
        return;
    }
    BLEScales.scan();
    JsonDocument doc;
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleConnect(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        request->send(404);
        return;
    }
    BLEScales.connect(request->arg("uuid").c_str());
    JsonDocument doc;
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleInfo(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["connected"] = BLEScales.isConnected();
    doc["name"] = BLEScales.getName();
    doc["uuid"] = BLEScales.getUUID();
    doc["rssi"] = BLEScales.getRSSI();
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}
#else // GAGGIMATE_ENABLE_BLE_SCALE

// BLE scale compiled out (CAR-386). The four /api/scales/* routes are registered
// unconditionally (see setupServer), so these stubs answer the flag-agnostic web
// bundle with a typed EMPTY payload and HTTP 200 -- never a 404. That keeps the
// Scales page in a clean "no devices" state instead of "Error loading devices".
// Payload shapes mirror the real handlers' so the client parses them unchanged.
void WebUIPlugin::handleBLEScaleList(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc.to<JsonArray>(); // empty array: no scales when BLE is compiled out
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleScan(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["success"] = false; // scanning unavailable: BLE scale support not built
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleConnect(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["success"] = false; // connecting unavailable: BLE scale support not built
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleInfo(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["connected"] = false;
    doc["name"] = "";
    doc["uuid"] = "";
    doc["rssi"] = 0;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    serializeJson(doc, *response);
    request->send(response);
}

#endif // GAGGIMATE_ENABLE_BLE_SCALE

void WebUIPlugin::updateOTAStatus(const String &version) {
    Settings const &settings = controller->getSettings();
    JsonDocument doc;
    doc["status"] = version;
    doc["tp"] = "res:ota-settings";
    doc["latestVersion"] = ota->getCurrentVersion();
    doc["displayUpdateAvailable"] = ota->isUpdateAvailable(false);
    doc["controllerUpdateAvailable"] = ota->isUpdateAvailable(true);
    doc["displayVersion"] = BUILD_GIT_VERSION;
    doc["controllerVersion"] = controller->getSystemInfo().version;
    doc["hardware"] = controller->getSystemInfo().hardware;
    doc["channel"] = settings.getOTAChannel();
    doc["updating"] = updating;
    // Surface the build-time list of selectable stable releases so the web UI
    // can render a "flash a specific tag" dropdown.
    {
        JsonArray arr = doc["availableVersions"].to<JsonArray>();
        for (size_t i = 0; i < STABLE_VERSIONS_COUNT; ++i) {
            arr.add(STABLE_VERSIONS[i]);
        }
    }
    // LittleFS usage metrics
    {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t freeBytes = total > used ? (total - used) : 0;
        doc["spiffsTotal"] = static_cast<uint32_t>(total);
        doc["spiffsUsed"] = static_cast<uint32_t>(used);
        doc["spiffsFree"] = static_cast<uint32_t>(freeBytes);
        if (total > 0) {
            // Provide integer percentage to avoid float JSON
            doc["spiffsUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
    if (controller->isSDCard()) {
        const uint64_t total = SD_MMC.cardSize();
        const uint64_t used = SD_MMC.usedBytes();
        const uint64_t freeBytes = total > used ? (total - used) : 0;
        doc["sdTotal"] = total;
        doc["sdUsed"] = used;
        doc["sdFree"] = freeBytes;
        if (total > 0) {
            // Provide integer percentage to avoid float JSON
            doc["sdUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
    broadcastAll(doc.as<String>());
}

void WebUIPlugin::updateOTAProgress(uint8_t phase, int progress) {
    JsonDocument doc;
    doc["tp"] = "evt:ota-progress";
    doc["phase"] = phase;
    doc["progress"] = progress;
    broadcastAll(doc.as<String>());
}

void WebUIPlugin::sendAutotuneResult() {
    JsonDocument doc;
    doc["tp"] = "evt:autotune-result";
    doc["pid"] = controller->getSettings().getPid();
    broadcastAll(doc.as<String>());
}

void WebUIPlugin::handleFlushStart(uint32_t clientId, JsonDocument &request) {
    controller->onFlush();

    JsonDocument response;
    response["tp"] = "res:flush:start";
    response["rid"] = request["rid"];
    response["success"] = true;

    sendResponse(clientId, response);
}

void WebUIPlugin::handleCoreDumpDownload(AsyncWebServerRequest *request) {
    // Check if core dump is available
    size_t coreAddr, coreSize;
    if (esp_core_dump_image_get(&coreAddr, &coreSize) != ESP_OK || coreSize == 0) {
        request->send(404, "text/plain", "No core dump available");
        return;
    }

    // Find the coredump partition
    const esp_partition_t *coredump_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (coredump_partition == NULL) {
        request->send(500, "text/plain", "Core dump partition not found");
        return;
    }

    ESP_LOGI("WebUIPlugin", "Streaming core dump: %d bytes from 0x%x", coreSize, coreAddr);

    // Create a streaming response
    AsyncWebServerResponse *response =
        request->beginResponse("application/octet-stream", coreSize,
                               [coredump_partition, coreSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                   // Calculate how much to read
                                   size_t remaining = coreSize - index;
                                   size_t toRead = (remaining < maxLen) ? remaining : maxLen;

                                   if (toRead == 0)
                                       return 0;

                                   // Read from partition
                                   esp_err_t err = esp_partition_read(coredump_partition, index, buffer, toRead);
                                   if (err != ESP_OK) {
                                       ESP_LOGE("WebUIPlugin", "Failed to read core dump: %s", esp_err_to_name(err));
                                       return 0;
                                   }

                                   return toRead;
                               });

    // Set appropriate headers
    response->addHeader("Content-Disposition", "attachment; filename=\"coredump.bin\"");
    response->addHeader("Cache-Control", "no-cache");
    addCorsHeaders(response);

    request->send(response);
}

void WebUIPlugin::handleDiagLogDownload(AsyncWebServerRequest *request, const char *sdPath) {
    // Gate on a mounted SD card. The diag log sink (PRO-268) only ever writes to
    // SD_MMC, so without a card there is nothing to serve — return a clean 404
    // rather than the SPA index. Independent of the diagnosticLog runtime flag:
    // a file may persist from a prior enabled session and is still downloadable.
    if (!controller->isSDCard()) {
        request->send(404, "text/plain", "No SD card");
        return;
    }
    if (!SD_MMC.exists(sdPath)) {
        request->send(404, "text/plain", "Log not found");
        return;
    }

    // Open the file and capture its size up front. The DiagnosticLogPlugin drain
    // task may append to (or rotate) the active file concurrently; FatFS is built
    // FF_FS_REENTRANT=1 so the per-volume mutex makes concurrent access safe.
    // We stream a best-effort snapshot bounded by the size at open: if the file
    // grows past it we simply don't serve the tail (no crash); if it is rotated
    // (renamed) out from under us the open handle keeps referencing the original
    // data, so the read still completes cleanly.
    auto file = std::make_shared<File>(SD_MMC.open(sdPath, FILE_READ));
    if (!*file || file->isDirectory()) {
        if (*file) {
            file->close();
        }
        request->send(404, "text/plain", "Log not found");
        return;
    }
    const size_t total = file->size();

    AsyncWebServerResponse *response =
        request->beginResponse("text/plain", total, [file, total](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= total) {
                file->close();
                return 0;
            }
            size_t remaining = total - index;
            size_t toRead = (remaining < maxLen) ? remaining : maxLen;
            // Best-effort read; a short read (e.g. file shrank under us) just ends
            // the stream early without crashing.
            int read = file->read(buffer, toRead);
            if (read <= 0) {
                file->close();
                return 0;
            }
            return static_cast<size_t>(read);
        });

    response->addHeader("Cache-Control", "no-store");
    addCorsHeaders(response);
    request->send(response);
}
