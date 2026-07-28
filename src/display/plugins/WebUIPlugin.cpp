#include "WebUIPlugin.h"
#include "OtaChannelSwitchPolicy.h"
#include "OtaIntentState.h"
#include "OtaResolveHeapPolicy.h"
#include "OtaResolveReusePolicy.h"
#include "OtaUpdateCheckPolicy.h"
#include "RelayConnectionPolicy.h"
#include <DNSServer.h>
#include <LittleFS.h>
#include <display/core/Controller.h>
#include <display/core/EventIds.h>
#include <display/core/GmHeapDiag.h> // PRO-566
#include <display/core/GrinderManager.h>
#include <display/core/MdnsNamePolicy.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/core/utils.h>
#include <display/models/profile.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <inttypes.h>

#include <SD_MMC.h>
#include <algorithm>
#include <cmath>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ChangeModeDeferPolicy.h>
#include <display/plugins/LocalAuthPolicy.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/plugins/StandbyReassertPolicy.h>
#include <display/plugins/StrictValidationPolicy.h>
#include <display/plugins/WsBroadcastClosePolicy.h>
#include <display/plugins/WsReassemblyPolicy.h>
#include <display/webassets/web_ui_manifest.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <version.h>

static std::unordered_map<uint32_t, std::string> rxBuffers;
static WebUIPlugin *g_webUIPlugin = nullptr;

// Sentinel value emitted on /api/settings GET in place of any stored secret
// (Wi-Fi password, Home Assistant password, cloud relay token). The POST
// handler treats incoming arguments equal to this string as "no change",
// preserving the stored value. Defined as a constexpr so the same literal is
// used at every read/write site.
static constexpr const char *kSecretSentinel = "---unchanged---";

// PRO-596: thin Arduino-String adapter over the pure, host-tested
// relay_connection_policy::relayTokenProtocol. The value logic lives in
// RelayConnectionPolicy.h (unit-tested under [env:native]); this wrapper only
// bridges Arduino `String` <-> `std::string` at the call boundary.
static String relayTokenProtocol(const String &token) {
    return String(relay_connection_policy::relayTokenProtocol(std::string(token.c_str())).c_str());
}

WebUIPlugin::WebUIPlugin() : server(80), ws("/ws") { g_webUIPlugin = this; }

// PRO-596: thin Arduino-String adapter over the pure, host-tested
// relay_connection_policy::parseRelayUrl. The out-param signature is preserved
// so startRelay() is unchanged; only the parsing body moved to the header.
static bool parseRelayUrl(const String &url, bool &useSSL, String &host, uint16_t &port, String &basePath) {
    const relay_connection_policy::RelayUrlParts parts = relay_connection_policy::parseRelayUrl(std::string(url.c_str()));
    if (!parts.valid) {
        return false;
    }
    useSSL = parts.useSSL;
    host = String(parts.host.c_str());
    port = parts.port;
    basePath = String(parts.basePath.c_str());
    return true;
}

void WebUIPlugin::addCorsHeaders(AsyncWebServerResponse *response) const {
#if defined(GAGGIMATE_DEVELOPMENT_CORS) && GAGGIMATE_DEVELOPMENT_CORS
    // Explicit local Vite development exception. Production LAN and AP setup use
    // same-origin requests and emit no CORS headers.
    if (localAuthShouldEmitCors(apMode, true)) {
        response->addHeader("Access-Control-Allow-Origin", "http://localhost:5173");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    }
#else
    (void)response;
#endif
}

void WebUIPlugin::handleOptions(AsyncWebServerRequest *request) const {
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCorsHeaders(response);
    request->send(response);
}

bool WebUIPlugin::isSetupBootstrapRequest(AsyncWebServerRequest *request) const {
    return request->method() == HTTP_GET && request->url() == "/api/settings";
}

bool WebUIPlugin::isHttpAuthenticated(AsyncWebServerRequest *request) const {
    if (localAuthMayBypassHttpInSetup(apMode, isSetupBootstrapRequest(request)))
        return true;
    const String queryToken = request->hasArg("localAuthToken") ? request->arg("localAuthToken") : String("");
    return localAuthHttpRequestAuthenticated(request->header("Authorization").c_str(), queryToken.c_str(),
                                             request->method() == HTTP_GET ? "GET" : "OTHER", request->url().c_str(),
                                             controller->getSettings().getLocalAdminToken().c_str());
}

void WebUIPlugin::sendUnauthorized(AsyncWebServerRequest *request) const {
    request->send(401, "application/json", "{\"error\":\"authentication required\"}");
}

bool WebUIPlugin::authenticateWebSocket(uint32_t clientId, JsonDocument &request) {
    const String token = request["token"].is<String>() ? request["token"].as<String>() : String("");
    const String authorization = String("Bearer ") + token;
    const bool authenticated =
        localAuthBearerMatches(authorization.c_str(), controller->getSettings().getLocalAdminToken().c_str());
    authenticatedWebSocketClients[clientId] = authenticated;
    return authenticated;
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
    if (controller->getSettings().getLocalAdminToken().isEmpty()) {
        char token[33];
        snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
                 static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
                 static_cast<unsigned long>(esp_random()));
        controller->getSettings().setLocalAdminToken(token);
        ESP_LOGW("WebUIPlugin", "Generated device-local admin token; retrieve it only through AP setup");
    }
    this->ota = new GitHubOTA(
        BUILD_GIT_VERSION, controller->getSystemInfo().version, resolveReleaseUrl(controller->getSettings().getOTAChannel()),
        [this](uint8_t phase) {
            pluginManager->trigger(EventIds::OTA_UPDATE_PHASE, "phase", phase);
            updateOTAProgress(phase, 0);
        },
        [this](uint8_t phase, int progress) {
            pluginManager->trigger(EventIds::OTA_UPDATE_PROGRESS, "progress", progress);
            updateOTAProgress(phase, progress);
        },
        "display-firmware.bin", "display-filesystem.bin", "board-firmware.bin");
    pluginManager->on(EventIds::CONTROLLER_WIFI_CONNECT, [this](Event const &event) {
        // PRO-417: do NOT call start() inline — this runs on the arduino_events
        // WiFi-event task. Latch the desired Start intent and let loop() (Arduino
        // loop task) run the actual start() off this task.
        // PRO-418: the captive-portal mode (AP vs STA) is folded INTO the intent
        // (StartAp / StartStation) so a single atomic carries both the "start"
        // decision and the mode. No separate pendingApMode atomic, hence no
        // cross-atomic ordering hazard — the deferred start() reads the mode
        // straight off the drained intent. Last event wins (latchLifecycleIntent
        // coalesces), and a later connect's mode overwrites an earlier one.
        pendingLifecycle.store(
            latchLifecycleIntent(pendingLifecycle.load(std::memory_order_relaxed), startIntentForApMode(event.getInt("AP") != 0)),
            std::memory_order_relaxed);
    });
    pluginManager->on(EventIds::CONTROLLER_WIFI_DISCONNECT, [this](Event const &) {
        // PRO-417: do NOT call stop() inline — stop()/stopRelay() blocks the
        // WiFi-event task (~500 ms spin-wait + ws.closeAll() under wsMutex) once
        // per ASSOC_LEAVE, stalling core 0 while WPA-supplicant re-associates.
        // Latch a Stop intent; loop() drains it on the Arduino loop task.
        pendingLifecycle.store(latchLifecycleIntent(pendingLifecycle.load(std::memory_order_relaxed), WebUiLifecycleIntent::Stop),
                               std::memory_order_relaxed);
    });
    pluginManager->on(EventIds::CONTROLLER_READY, [this](Event const &) {
        ota->setControllerVersion(controller->getSystemInfo().version);
        ota->init(controller->getClientController()->getClient());
    });
    pluginManager->on(EventIds::CONTROLLER_AUTOTUNE_RESULT, [this](Event const &event) { sendAutotuneResult(); });

    // Forward shot history rebuild progress events to WebSocket clients
    pluginManager->on(EventIds::EVT_HISTORY_REBUILD_PROGRESS, [this](Event const &event) {
        JsonDocument doc;
        doc["tp"] = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
        doc["total"] = event.getInt("total");
        doc["current"] = event.getInt("current");
        doc["status"] = event.getString("status");
        broadcastAll(doc.as<String>());
    });

    // Subscribe to Bluetooth scale weight updates
    pluginManager->on(EventIds::CONTROLLER_VOLUMETRIC_MEASUREMENT_BLUETOOTH_CHANGE,
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

// PRO-13: one-shot resolve task for the forced-tag/channel-switch OTA path.
// Runs `ota->checkForUpdates()` (the blocking HTTPS GET) OFF the loop task,
// then posts the resolved OtaFlashDecision back under otaIntentMutex and
// self-deletes. `ota` itself is not otherwise touched concurrently while
// this task runs: the periodic background checkForUpdates() call (line
// ~485, PRO-411) and the release-URL/status-push drains above it all run on
// the loop task, and loop() never calls into `ota` again for THIS forced
// update until it observes ReadyToFlash/Failed and this task has already
// returned — so there is exactly one task touching `ota` at any given
// instant, matching the pre-PRO-13 single-task-owns-ota invariant.
void WebUIPlugin::otaResolveTask(void *arg) {
    auto *params = static_cast<OtaResolveTaskParams *>(arg);
    WebUIPlugin *plugin = params->plugin;
    GitHubOTA *ota = plugin->ota;

    // PRO-556: before opening a SECOND, independent HTTPS/TLS connection, try to
    // reuse the periodic background OTA check's already-resolved head. The
    // periodic check (loop(), PRO-411/PRO-555) runs checkForUpdates() on this
    // same `ota` instance every ~5 min and caches getCurrentVersion(). If it
    // last succeeded against the SAME channel we are resolving for and that
    // result is fresh (< half the check interval old), reusing it avoids an
    // entire TLS handshake — eliminating that handshake's memory-pressure /
    // latency / cert-verify exposure from this click-driven path and reducing
    // load on github.com. The snapshot (channel/version/failed/timestamp) was
    // taken by value on the loop task at spawn time, so this decision races
    // nothing. When reuse is refused (different channel, too stale, empty, or
    // the periodic check itself failed/deferred under PRO-555) we fall through
    // to the existing independent checkForUpdates() call, STILL protected by the
    // PRO-554 heap guard below.
    const bool canReusePeriodic = otaResolveCanReusePeriodic(
        params->haveEverChecked, params->periodicFailed, params->periodicVersion.c_str(), params->periodicChannel.c_str(),
        params->resolveChannel.c_str(), params->periodicResolvedAtMs, static_cast<uint32_t>(millis()));

    String resolved;
    bool resolveFailed;
    if (canReusePeriodic) {
        ESP_LOGI("WebUIPlugin",
                 "Reusing fresh periodic OTA check result for channel '%s' (head '%s') — skipping redundant TLS handshake",
                 params->resolveChannel.c_str(), params->periodicVersion.c_str());
        resolved = params->periodicVersion;
        resolveFailed = false;
    } else {
        // PRO-554: pre-flight internal-DRAM guard. checkForUpdates() below opens a
        // FRESH, independent HTTPS/TLS connection to GitHub (a second one, on top of
        // the periodic background check's own WiFiClientSecure). Under current
        // internal-DRAM pressure (same failure class as PRO-334/PRO-358) that extra
        // TLS handshake can trip an OOM deep in mbedtls's certificate-verify path,
        // which on this ESP-IDF/mbedtls port PANICS (LoadProhibited) instead of
        // failing cleanly — crash-looping the device on every channel-switch click.
        // If the largest contiguous free internal-DRAM block is below the floor,
        // skip the TLS attempt entirely and fail the resolve closed, mirroring the
        // xTaskCreatePinnedToCore-OOM branch in loop()'s Idle case: an empty
        // resolved version drives decideOtaFlash() to Refuse -> OtaResolveState::
        // Failed -> "Update failed" surfaced to the UI, never a panic.
        const size_t largestFreeInternalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        const bool heapSufficient = otaResolveHeapSufficient(largestFreeInternalBlock);

        if (!heapSufficient) {
            ESP_LOGE("WebUIPlugin",
                     "Skipping OTA resolve TLS handshake: largest free internal DRAM block %u < floor %u — failing closed to "
                     "avoid an mbedtls OOM panic",
                     static_cast<unsigned>(largestFreeInternalBlock), static_cast<unsigned>(kOtaResolveInternalDramFloorBytes));
            resolveFailed = true;
        } else {
            ota->checkForUpdates();
            resolved = ota->getCurrentVersion();
            // resolveFailed at this layer == the last checkForUpdates() failed to
            // resolve a head (network error / GitHub redirect quirk / malformed
            // channel). We consult the AUTHORITATIVE failure flag
            // (isUpdateCheckFailed()) rather than emptiness alone: on a failed
            // resolve getCurrentVersion() returns the STALE version string from a
            // prior successful check, so a periodic check that already populated a
            // version would otherwise mask a failed channel-switch resolve and
            // force-flash against a stale _latest_url. Keep the || isEmpty() as a
            // belt-and-suspenders guard (empty is untrustworthy).
            resolveFailed = ota->isUpdateCheckFailed() || resolved.isEmpty();
        }
    }
    const OtaFlashDecision decision = decideOtaFlash(params->isTag, params->pinnedTag.c_str(), params->selectedEqInstalled,
                                                     params->installedEmpty, resolved.c_str(), resolveFailed);

    if (plugin->otaIntentMutex != nullptr && xSemaphoreTake(plugin->otaIntentMutex, portMAX_DELAY) == pdTRUE) {
        plugin->otaResolveResult.generation = params->generation;
        plugin->otaResolveResult.decision = decision;
        plugin->otaResolveResult.resolvedVersion = resolved;
        plugin->otaResolveResult.resolveFailed = resolveFailed;
        plugin->otaResolveResultReady = true;
        xSemaphoreGive(plugin->otaIntentMutex);
    }
    delete params;
    vTaskDelete(nullptr);
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
    // PRO-417: drain the deferred web-server lifecycle intent latched by the WiFi
    // (dis)connect event handlers. Runs FIRST, on the Arduino loop task, so the
    // heavy start()/stop() (stopRelay()'s bounded spin-wait + ws.closeAll() under
    // wsMutex) executes here instead of inline on the arduino_events WiFi-event
    // task, where doing it once per ASSOC_LEAVE stalled core 0 while WPA-supplicant
    // re-associated. Must stay ABOVE the `if (!serverRunning) return;` guard below
    // so a queued Start (server currently down) is not stranded; a Stop while the
    // server is down is a no-op inside stop() (its own `!serverRunning` guard).
    // Read-and-clear atomically so a WiFi event that arrives mid-drain re-latches
    // for the next tick rather than being lost.
    {
        const WebUiLifecycleIntent latched = pendingLifecycle.exchange(WebUiLifecycleIntent::None, std::memory_order_relaxed);
        const WebUiLifecycleAction action = lifecycleDrainAction(latched);
        switch (action) {
        case WebUiLifecycleAction::RunStartStation:
        case WebUiLifecycleAction::RunStartAp:
            // PRO-418: the AP mode rides on the drained intent itself (single
            // atomic), so there is no second pendingApMode load that could be
            // stale relative to the intent.
            apMode = drainActionIsAp(action);
            start();
            break;
        case WebUiLifecycleAction::RunStop:
            stop();
            break;
        case WebUiLifecycleAction::None:
        default:
            break;
        }
    }
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
            const OtaDeferredDrainResult drained = drainOtaDeferredIntent(pendingOtaStart, pendingUpdateComponent.c_str());
            updateComponent = drained.payload.c_str();
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
        // a deferral is only ever armed for a target that defers — a non-STANDBY
        // target (any settle window) or an AUTOMATIC standby-on-brew STANDBY
        // (PRO-587). An EXPLICIT STANDBY never arms, so a STANDBY sitting in
        // pendingModeChangeTarget is by construction automatic. Passing
        // automatic=true here therefore keeps the hold/apply decision in
        // lock-step with the arming gate for BOTH cases: it reduces to the pure
        // settle-window check (isExtendedRecording) — hold while the window is
        // open, apply the moment it closes — with no separate pending-auto flag.
        if (shouldDeferModeChange(pendingModeChangeTarget, ShotHistory.isExtendedRecording(), /*automatic=*/true)) {
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
                // PRO-589: intentionally does NOT set lastExplicitStandbyMs /
                // sawExplicitStandby here, unlike the immediate-apply branch in the
                // req:change-mode handler. Since PRO-587 a `target` of MODE_STANDBY
                // CAN reach this drain path (an automatic standby-on-brew defers
                // through the settle window), but it is always an *automatic*
                // standby-on-brew — an explicit STANDBY never defers. Standby-on-brew
                // and auto-steam are mutually exclusive in the UI, so there is no
                // reflexive auto-steam STEAM re-fire to bounce off this STANDBY;
                // PRO-421's reassert guard (which reads those two fields) therefore
                // cannot be triggered via the deferred-apply path, and leaving it
                // unarmed here is intentional and safe.
                controller->setMode(target);
            }
        }
    }
    if (updating) {
        // PRO-400: force-flash whenever the user pinned a specific tag
        // (e.g. "tag:2.0.8") OR switched channels (stable <-> beta <-> nightly).
        // A tag bypasses the upgrade-only guard so re-flashing the same version
        // and downgrading both work. A channel switch must ALWAYS flash the
        // resolved head of the newly-selected channel regardless of semver
        // direction (beta->stable / nightly->stable are lower/equal semver and
        // would otherwise stall on the upgrade-only guard), while upgrades
        // WITHIN a channel still run the guard. See OtaChannelSwitchPolicy.h.
        //
        // PRO-13: a tag pin OR a channel switch needs a resolve + confirm
        // before we can trust what would be flashed (this also defeats the
        // stale-_latest_url race the tag: path documents: a WS client can
        // send `req:ota-settings <channel>` then `req:ota-start` before the
        // throttled checkForUpdates() in this same loop runs). That resolve
        // used to run SYNCHRONOUSLY on this task via ota->checkForUpdates(),
        // blocking loop() (and the 200ms evt:status broadcast) for however
        // long the blocking HTTPS GET to github.com takes. It now runs on a
        // one-shot FreeRTOS task; this block only ever calls ota->update()
        // once that task reports READY_TO_FLASH (or runs the plain
        // upgrade-only path immediately when neither a tag nor a channel
        // switch is in play — that path never touched checkForUpdates() here
        // to begin with, so it is unaffected by this change).
        // PRO-448: these live reads re-run on EVERY loop() tick while `updating`
        // is true, but they only matter in two places: (1) the plain
        // within-channel-upgrade path just below, which runs this block fresh
        // each tick since it never enters the resolve state machine; and (2) the
        // Idle case of the switch below, which latches channel/previousInstalledChannel/
        // isTag/channelSwitch into the otaResolve* fields once, at spawn time.
        // Once otaResolveState leaves Idle (Resolving/ReadyToFlash/Failed), the
        // state machine is driven entirely by those latched otaResolve* fields —
        // these live locals are computed again on every tick but ignored for the
        // rest of the resolve lifecycle. Note `settings` itself is NOT idle-only:
        // it's also referenced in the ReadyToFlash case (~line 516) to persist
        // installedChannel, so it can't be scoped inside an Idle-only block.
        Settings &settings = controller->getSettings();
        const String channel = settings.getOTAChannel();
        const String previousInstalledChannel = settings.getInstalledChannel();
        const bool isTag = channel.startsWith("tag:");
        const bool channelSwitch = !isTag && channel != previousInstalledChannel;

        if (!(isTag || channelSwitch)) {
            // Plain within-channel upgrade: no resolve needed, run exactly the
            // pre-existing immediate path (unaffected by PRO-13).
            pluginManager->trigger(EventIds::OTA_UPDATE_START);
            const OtaComponentSelection componentSelection = selectOtaComponents(updateComponent.c_str());
            const bool updateSucceeded =
                ota->update(componentSelection.updateController, componentSelection.updateDisplay, /*force=*/false);
            pluginManager->trigger(EventIds::OTA_UPDATE_END);
            updating = false;
            if (!updateSucceeded) {
                updateOTAStatus("Update failed");
            }
        } else {
            switch (otaResolveState) {
            case OtaResolveState::Idle: {
                // First loop() iteration of this forced-tag/channel-switch OTA:
                // latch the decision inputs at spawn time and kick off the
                // one-shot resolve task. EventIds::OTA_UPDATE_START fires here (not
                // only on the eventual flash) so the UI's updateActive/standby
                // transition (DefaultUI's ota:update:start handler) still
                // engages immediately, matching pre-PRO-13 behavior.
                pluginManager->trigger(EventIds::OTA_UPDATE_START);
                otaResolveChannel = channel;
                otaResolvePinnedTag = isTag ? channel.substring(4) : String("");
                otaResolveIsTag = isTag;
                otaResolveChannelSwitch = channelSwitch;
                otaResolvePreviousInstalledChannel = previousInstalledChannel;
                otaResolveResolvedVersion = "";
                otaResolveResolveFailed = false;
                otaResolveTimedOutFlag = false;
                otaResolveStartMs = static_cast<uint32_t>(millis());
                const uint32_t generation = otaResolveGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

                auto *params = new OtaResolveTaskParams{this, generation, isTag, otaResolvePinnedTag,
                                                        /*selectedEqInstalled=*/!channelSwitch,
                                                        /*installedEmpty=*/previousInstalledChannel.isEmpty(),
                                                        // PRO-556: snapshot the periodic check's cached result on
                                                        // the loop task, before the resolve task exists, so the
                                                        // task can decide whether to reuse it (same channel +
                                                        // fresh + succeeded) instead of opening a 2nd TLS conn.
                                                        /*resolveChannel=*/channel,
                                                        /*periodicChannel=*/otaPeriodicResolvedChannel,
                                                        /*periodicVersion=*/ota->getCurrentVersion(),
                                                        /*periodicFailed=*/ota->isUpdateCheckFailed(),
                                                        /*haveEverChecked=*/lastUpdateCheck != 0,
                                                        /*periodicResolvedAtMs=*/static_cast<uint32_t>(lastUpdateCheck)};
                TaskHandle_t createdHandle = nullptr;
                const BaseType_t created =
                    xTaskCreatePinnedToCore(otaResolveTask, "OtaResolve", 8192, params, 1, &createdHandle, 1);
                if (created != pdPASS) {
                    // OOM spawning the resolve task: fail closed exactly like a
                    // resolve failure would, rather than getting stuck IDLE
                    // forever with `updating` latched true.
                    ESP_LOGE("WebUIPlugin", "Failed to create OTA resolve task (OOM)");
                    delete params;
                    otaResolveResolveFailed = true;
                    otaResolveState = OtaResolveState::Failed;
                } else {
                    otaResolveState = OtaResolveState::Resolving;
                    updateOTAStatus("Verifying release...");
                }
                break;
            }
            case OtaResolveState::Resolving: {
                // Drain a posted resolve-task result (if any) under otaIntentMutex,
                // mirroring the existing release-URL/OTA-start intent handoffs.
                bool haveResult = false;
                OtaResolveTaskResult drainedResult;
                if (otaResolveResultReady && otaIntentMutex != nullptr &&
                    xSemaphoreTake(otaIntentMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    // Re-check under the lock: the outer check above is a fast-path
                    // skip (avoid taking the mutex when there's obviously nothing to
                    // drain), not a guarantee. otaResolveResultReady is volatile, so
                    // this inner read is the authoritative TOCTOU guard. In practice
                    // the resolve task is the only writer (sets it once, then
                    // self-deletes) and loop() is the only clearer, so the flag
                    // cannot flip between the two checks today — but the guard is
                    // cheap and keeps the drain correct if that ever changes.
                    if (otaResolveResultReady) {
                        drainedResult = otaResolveResult;
                        otaResolveResultReady = false;
                        haveResult = true;
                    }
                    xSemaphoreGive(otaIntentMutex);
                }
                if (haveResult &&
                    otaResolveResultIsCurrent(drainedResult.generation, otaResolveGeneration.load(std::memory_order_relaxed))) {
                    otaResolveResolvedVersion = drainedResult.resolvedVersion;
                    otaResolveResolveFailed = drainedResult.resolveFailed;
                    otaResolveState = otaResolveStateForDecision(drainedResult.decision);
                    if (otaResolveState == OtaResolveState::Failed) {
                        if (otaResolveIsTag) {
                            ESP_LOGE("WebUIPlugin", "Refusing forced OTA: pinned tag %s but resolved %s",
                                     otaResolvePinnedTag.c_str(), otaResolveResolvedVersion.c_str());
                        } else {
                            ESP_LOGE("WebUIPlugin", "Refusing channel-switch OTA to %s: resolve failed",
                                     otaResolveChannel.c_str());
                        }
                    }
                } else if (otaResolveTimedOut(otaResolveStartMs, static_cast<uint32_t>(millis()), kOtaResolveTimeoutMs)) {
                    // Soft 10s timeout: abandon the in-flight resolve. Bump the
                    // generation so the task's eventual (late) result is
                    // recognized as stale and dropped when/if it arrives.
                    otaResolveGeneration.fetch_add(1, std::memory_order_relaxed);
                    otaResolveTimedOutFlag = true;
                    otaResolveResolveFailed = true;
                    otaResolveState = OtaResolveState::Failed;
                    const String &verifyTarget = otaResolveIsTag ? otaResolvePinnedTag : otaResolveChannel;
                    ESP_LOGE("WebUIPlugin", "OTA resolve timed out after %" PRIu32 "ms verifying %s", kOtaResolveTimeoutMs,
                             verifyTarget.c_str());
                }
                // else: still resolving, no result yet, not timed out — stay in
                // RESOLVING and re-check next loop() tick. "Verifying release..."
                // was already pushed once on the Idle->Resolving transition
                // above, so nothing more to push here (not spammed every tick).
                break;
            }
            case OtaResolveState::ReadyToFlash: {
                // force=true for a confirmed tag pin OR a confirmed channel
                // switch — the async resolve path is only ever entered for one
                // of those two, so force is unconditionally true here.
                const bool forceChannelSwitch = otaResolveChannelSwitch;
                const bool force = true;
                // On a confirmed channel switch, persist installedChannel =
                // otaChannel BEFORE update() (the success path reboots via
                // GitHubOTA and never returns). Restored below if update()
                // returns false (no reboot).
                if (forceChannelSwitch) {
                    settings.setInstalledChannel(otaResolveChannel);
                }
                const OtaComponentSelection componentSelection = selectOtaComponents(updateComponent.c_str());
                const bool updateSucceeded =
                    ota->update(componentSelection.updateController, componentSelection.updateDisplay, force);
                pluginManager->trigger(EventIds::OTA_UPDATE_END);
                updating = false;
                otaResolveState = OtaResolveState::Idle;
                if (!updateSucceeded) {
                    // update() returned (no reboot) — restore the previous
                    // installedChannel so a failed switch doesn't leave the
                    // persisted installed marker ahead of what is actually
                    // flashed.
                    //
                    // PRO-403: but only when NO component was actually flashed.
                    // On the default two-component flash the controller can
                    // flash OK and the display then fail; update() returns
                    // false while the controller is already running the new
                    // channel's head. installedChannel is a whole-device
                    // marker feeding the next channel-switch decision and the
                    // PRO-401 pending-switch UI hint, so on such a partial
                    // flash we must KEEP installedChannel = channel (the new
                    // one) to reflect the controller's actual on-device state.
                    // Only restore when the controller was not flashed (the
                    // old channel is still what runs).
                    if (forceChannelSwitch && !ota->didFlashControllerLastUpdate()) {
                        settings.setInstalledChannel(otaResolvePreviousInstalledChannel);
                    }
                    updateOTAStatus("Update failed");
                }
                break;
            }
            case OtaResolveState::Failed:
            default: {
                pluginManager->trigger(EventIds::OTA_UPDATE_END);
                updating = false;
                otaResolveState = OtaResolveState::Idle;
                if (otaResolveTimedOutFlag) {
                    const String verifyTarget = otaResolveIsTag ? otaResolvePinnedTag : otaResolveChannel;
                    updateOTAStatus("Could not verify release " + verifyTarget + " — check network");
                } else {
                    updateOTAStatus("Update failed (tag not resolved)");
                }
                break;
            }
            }
        }
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
            const OtaDeferredDrainResult drained = drainOtaDeferredIntent(pendingReleaseUrlChange, pendingReleaseUrl.c_str());
            url = drained.payload.c_str();
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
    // PRO-411: back off the periodic OTA update-check after consecutive
    // failures instead of always retrying every UPDATE_CHECK_INTERVAL. A failing
    // check opens a fresh TLS connection to github.com concurrently with
    // async_tcp/wifi/mdns; on a persistent failure that both hammers github.com
    // and gives the (now-guarded) connect path repeated chances to misbehave.
    // The effective interval doubles per consecutive failure up to
    // UPDATE_CHECK_MAX_INTERVAL and resets on the first success. The device stays
    // online and responsive throughout — checkForUpdates() failures are logged
    // and swallowed, never fatal.
    const unsigned long effectiveInterval = otaBackoffInterval(
        static_cast<uint32_t>(UPDATE_CHECK_INTERVAL), static_cast<uint32_t>(UPDATE_CHECK_MAX_INTERVAL), otaCheckFailureCount);
    if (lastUpdateCheck == 0 || now - lastUpdateCheck > effectiveInterval) {
        // PRO-560: skip the periodic check while a click-driven resolve
        // (otaResolveTask, PRO-13) is in flight. Both this loop-task path and the
        // resolve task's non-reuse fallback call ota->checkForUpdates() on the
        // SAME non-reentrant GitHubOTA instance; if this interval elapses mid-
        // resolve they could run concurrently (the long-standing PRO-13/PRO-411
        // race, only narrowed by PRO-556's reuse hit). otaResolveState is loop-
        // task-owned so this read needs no mutex, and a resolve only touches `ota`
        // while Resolving — skipping here gives mutual exclusion by construction.
        // PRO-563: that "while Resolving" guard has a residual gap — a resolve
        // abandoned on the soft 10s timeout is never force-killed (PRO-13 point 7)
        // and its task can keep touching `ota` for a few more seconds AFTER state
        // has left Resolving. otaPeriodicCheckShouldSkip() therefore ALSO keeps the
        // check skipped through a bounded post-timeout grace window (keyed off the
        // same otaResolveStartMs / kOtaResolveTimeoutMs, gated on the timeout flag).
        // Like the PRO-555 defer below, this is a SKIP not a failure: do NOT bump
        // otaCheckFailureCount (the check never ran) and do NOT advance
        // lastUpdateCheck, so the check retries promptly next loop pass once the
        // resolve settles out of Resolving and the grace window closes.
        if (!otaPeriodicCheckShouldSkip(OtaResolveSnapshot{otaResolveState, otaResolveTimedOutFlag, otaResolveStartMs,
                                                           static_cast<uint32_t>(now), kOtaResolveTimeoutMs,
                                                           kOtaResolveAbandonGraceMs})) {
            if (otaPeriodicCheckShouldDefer(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))) {
                // PRO-557: the defer branch deliberately does NOT advance
                // lastUpdateCheck (so the DRAM re-check retries every loop tick until
                // pressure clears), which keeps this outer guard true every ~2 ms
                // tick. Rate-limit ONLY the log so it does not fire at ~500 Hz for
                // the whole pressure window — flooding synchronous UART (diagnostics
                // off) or the bounded DiagnosticLogPlugin queue (diagnostics on).
                // The DRAM re-check cadence is untouched.
                if (otaDeferLogShouldEmit(lastOtaDeferLogMs, static_cast<uint32_t>(now), kOtaDeferLogCooldownMs,
                                          otaDeferLogged)) {
                    ESP_LOGW("WebUIPlugin",
                             "Deferring periodic OTA check: largest free internal DRAM block < %u floor — retrying next loop",
                             static_cast<unsigned>(kOtaResolveInternalDramFloorBytes));
                    lastOtaDeferLogMs = static_cast<uint32_t>(now);
                    otaDeferLogged = true;
                }
            } else {
                // PRO-557: DRAM pressure cleared and the check actually ran — reset
                // the defer-log gate so the FIRST defer of any future pressure window
                // logs immediately again (rather than being suppressed by a stale
                // in-cooldown timestamp from the previous window).
                otaDeferLogged = false;
                ota->checkForUpdates();
                if (ota->isUpdateCheckFailed()) {
                    if (otaCheckFailureCount < UINT32_MAX) {
                        otaCheckFailureCount++;
                    }
                } else {
                    otaCheckFailureCount = 0;
                    // PRO-556: record WHICH channel this successful check resolved
                    // against so a subsequent click-driven resolve (otaResolveTask)
                    // can safely reuse ota->getCurrentVersion() for the SAME channel
                    // instead of opening a second TLS connection. Only updated on
                    // success — a failed check leaves a stale head and must not be
                    // reused (see OtaResolveReusePolicy.h). Deferred checks (PRO-555)
                    // never reach this branch, so neither this nor lastUpdateCheck
                    // advances, and reuse is correctly refused.
                    otaPeriodicResolvedChannel = controller->getSettings().getOTAChannel();
                }
                pluginManager->trigger(EventIds::OTA_UPDATE_STATUS, "value", ota->isUpdateAvailable());
                lastUpdateCheck = now;
                updateOTAStatus(ota->getCurrentVersion());
            }
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
        doc["gr"] = controller->getSettings().getSelectedGrinder();
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
        doc["sb"] = controller->getSettings().isStandbyOnBrewEnabled() ? 1 : 0;
        // Round dose grams to 1 decimal so the wire value matches the "float" web contract
        // (avoids noisy full-precision doubles; firmware keeps full precision internally).
        doc["dg"] = std::round(controller->getSettings().getDoseGrams() * 10.0) / 10.0;
        // PRO-603: device-authoritative manual grinder-dial setting, rounded to
        // 1 decimal like "dg". Kept in sync across browsers via req:manual-grind:set.
        doc["mg"] = std::round(controller->getSettings().getManualGrindSetting() * 10.0) / 10.0;
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
    server.on("/api/settings/provision", HTTP_POST,
              [this](AsyncWebServerRequest *request) { handleSettingsProvisioning(request); });
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
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleBLEScaleList(request);
    });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleBLEScaleConnect(request);
    });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleBLEScaleScan(request);
    });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleBLEScaleInfo(request);
    });
    FS *fs = &LittleFS;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
    }
    server.serveStatic("/api/history/", *fs, "/h/").setCacheControl("no-store").setFilter([this](AsyncWebServerRequest *request) {
        const bool authenticated = isHttpAuthenticated(request);
        if (!authenticated)
            sendUnauthorized(request);
        return authenticated;
    });
    server.on("/api/history/index.bin", HTTP_GET, [this, fs](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        if (fs->exists("/h/index.bin")) {
            AsyncWebServerResponse *response = request->beginResponse(*fs, "/h/index.bin", "application/octet-stream");
            response->addHeader("Cache-Control", "no-store");
            addCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "text/plain", "Index not found");
        }
    });
    server.on("/api/core-dump", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleCoreDumpDownload(request);
    });
    // Diagnostic SD log download (PRO-274). Explicit handlers (not serveStatic)
    // so a missing file returns a clean 404 instead of falling through to the
    // SPA catch-all (onNotFound) below. Registered before onNotFound so these
    // /api/-prefixed routes win. Served regardless of the diagnosticLog flag —
    // the file may exist from a prior enabled session — but gated on a mounted
    // SD card inside the handler. Paths mirror DiagnosticLogPlugin::SD_LOG_PATH /
    // SD_LOG_PATH_OLD; literals are used here rather than including that plugin's
    // header (it pulls WiFiUdp.h, which the native display-sim build can't resolve).
    server.on("/api/diag/log.txt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleDiagLogDownload(request, "/diag/log.txt");
    });
    server.on("/api/diag/log.1", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isHttpAuthenticated(request))
            return sendUnauthorized(request);
        handleDiagLogDownload(request, "/diag/log.1");
    });
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
                // PRO-357: do NOT enable setCloseClientOnQueueFull(true). With it
                // enabled, AsyncWebSocketClient::_queueMessage() reacts to a full
                // TX queue by calling _client->close() INLINE, synchronously, on
                // whatever task is sending — which (per ESPAsyncWebServer v3.9.1)
                // drives ~AsyncWebSocketClient() -> _handleEvent(WS_EVT_DISCONNECT)
                // right here in this same onEvent handler. When the send is
                // loopTask's broadcastAll() -> ws.textAll() (which holds wsMutex),
                // that inline disconnect branch RE-TAKES the non-recursive wsMutex
                // on the same task -> self-deadlock; the AsyncTCP task then blocks
                // forever on wsMutex too and the Task Watchdog reboots the board
                // (PRO-357 coredump: "Task watchdog got triggered ... async_tcp").
                // Leaving it false makes a full queue DROP the new frame (queue is
                // hard-capped at WS_MAX_QUEUED_MESSAGES, so no unbounded growth)
                // instead of force-closing; the periodic evt:status heartbeat
                // resends fresh state on the next tick, and a genuinely dead
                // connection is still reaped by AsyncTCP's own _onTimeout->close()
                // (which runs on the AsyncTCP task, NOT under a held wsMutex). The
                // explicit abuse-close on the WS_EVT_DATA reassembly-cap path
                // (handleWebSocketData -> client->close(1009)) is unaffected and
                // still runs. See broadcastAll() and the `ws` invariant.
                //
                // Pin the decision (PRO-357): broadcastAll() sends under wsMutex
                // and wsMutex is non-recursive, so the library's inline close is
                // NOT safe to enable here. If a future change makes the send
                // lock-free or the mutex recursive, this assert documents what
                // must be re-evaluated before re-enabling setCloseClientOnQueueFull.
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
                authenticatedWebSocketClients.erase(client->id());
            } else if (type == WS_EVT_DATA) {
                handleWebSocketData(server, client, type, arg, data, len);
            }
        });
    server.addHandler(&ws);
}

void WebUIPlugin::start() {
    stop();
    GM_HEAP_DIAG("before webserver+relay start"); // PRO-566
    server.begin();
    ESP_LOGI("WebUIPlugin", "Started webserver");
    if (apMode) {
        dnsServer = new DNSServer();
        dnsServer->setTTL(3600);
        dnsServer->start(53, "*", WIFI_AP_IP);
        ESP_LOGI("WebUIPlugin", "Started catchall DNS for captive portal");
    }
    lastUpdateCheck = 0;
    // PRO-562: reset the defer-log gate alongside the periodic-check timer so the
    // first deferred-check log after startup isn't suppressed by a stale in-cooldown
    // timestamp carried over from before the restart (mirrors the reset at ~L806).
    otaDeferLogged = false;
    serverRunning = true;
    startRelay();
    GM_HEAP_DIAG("after webserver+relay start"); // PRO-566
}

void WebUIPlugin::stop() {
    stopRelay();
    if (!serverRunning)
        return;
    server.end();
    {
        // PRO-313: closeAll() walks the client list. Since PRO-417 stop() runs on
        // the Arduino loop task (drained from a WiFi-event latch), not inline on
        // the arduino_events WiFi-event task — but the wsMutex is still required to
        // serialize this walk against the AsyncTCP task's client-list mutation
        // (connect/disconnect), which is a different task on a different core.
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
    // FreeRTOS tasks — since PRO-417 start()/stop() run on the Arduino loop task
    // (drained from a WiFi-event latch on controller:wifi:connect/disconnect),
    // while handleSettings() runs on the AsyncTCP /api/settings task and calls
    // stopRelay()+startRelay() back-to-back. A WiFi (dis)connect drain can
    // therefore still genuinely interleave with a cloud-relay settings toggle.
    // They are serialized by relayLifecycleMutex (taken at the
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

    String path = String(relay_connection_policy::resolveRelayConnectPath(std::string(basePath.c_str())).c_str());
    String relayProtocols = "Sec-WebSocket-Protocol: gaggimate-relay-v1, " + relayTokenProtocol(relayToken) + "\r\n";
    relayWs.setExtraHeaders(relayProtocols.c_str());

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

    // SSL heap usage can reach 50 KB; bail early rather than destabilize the device.
    if (useSSL && esp_get_free_heap_size() < 60000) {
        ESP_LOGW("WebUIPlugin", "Insufficient heap (%u B) for SSL relay — skipping",
                 static_cast<unsigned>(esp_get_free_heap_size()));
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
        // on the Arduino loop task via the PRO-417 lifecycle drain, or the AsyncTCP
        // web-server task via handleSettings()). Loop cadence is 10 ms;
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
    if (clientId != RELAY_CLIENT_ID && msgType == "req:auth") {
        JsonDocument response;
        response["tp"] = "res:auth";
        response["ok"] = authenticateWebSocket(clientId, doc);
        if (!response["ok"].as<bool>())
            response["error"] = "Authentication failed";
        sendResponse(clientId, response);
        return;
    }
    const bool sessionAuthenticated =
        clientId != RELAY_CLIENT_ID && localAuthWebSocketSessionAuthenticated(authenticatedWebSocketClients, clientId);
    if (!localAuthWebSocketMessageAllowed(clientId == RELAY_CLIENT_ID, sessionAuthenticated, msgType.c_str())) {
        ESP_LOGW("WebUIPlugin", "Rejected unauthenticated WebSocket request: %s", msgType.c_str());
        return;
    }

    // PRO-521: command payload boundary. Known fields on control requests must
    // have the documented JSON scalar type/range before any dispatch side effect.
    // Unknown fields are deliberately ignored for forward compatibility.
    static const char *const commandFields[] = {"target", "grams",       "mode", "targetType", "pressure",
                                                "flow",   "temperature", "time", "samples",    "value"};
    strict_validation::Fields commandValues;
    for (const char *field : commandFields) {
        const bool applies = (msgType == "req:change-grind-target" && String(field) == "target") ||
                             (msgType == "req:change-mode" && String(field) == "mode") ||
                             (msgType == "req:change-brew-target" && String(field) == "target") ||
                             (msgType == "req:dose:set" && String(field) == "grams") ||
                             (msgType == "req:manual-grind:set" && String(field) == "value") ||
                             (msgType == "req:manual:update") ||
                             (msgType == "req:autotune-start" && (String(field) == "time" || String(field) == "samples"));
        if (!applies)
            continue;
        const bool required = (msgType == "req:change-grind-target" && String(field) == "target") ||
                              (msgType == "req:change-mode" && String(field) == "mode") ||
                              (msgType == "req:change-brew-target" && String(field) == "target") ||
                              (msgType == "req:dose:set" && String(field) == "grams") ||
                              (msgType == "req:manual-grind:set" && String(field) == "value");
        JsonVariantConst value = doc[field];
        if (value.isNull()) {
            if (required)
                commandValues.push_back({field, ""});
            continue;
        }
        const bool numericField = String(field) != "targetType";
        const bool isNumeric = value.is<int>() || value.is<float>();
        if ((numericField && !isNumeric) || (!numericField && !value.is<const char *>())) {
            ESP_LOGW("WebUIPlugin", "Ignoring %s: invalid %s type", msgType.c_str(), field);
            return;
        }
        commandValues.push_back({field, value.as<String>().c_str(), isNumeric});
    }
    strict_validation::Error commandError;
    if (!strict_validation::validateWebSocketRequest(msgType.c_str(), commandValues, commandError)) {
        ESP_LOGW("WebUIPlugin", "Ignoring %s: invalid %s", msgType.c_str(), commandError.field.c_str());
        return;
    }
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
            // PRO-587: an OPTIONAL `auto` flag distinguishes an AUTOMATIC,
            // post-shot standby-on-brew transition (sent by the web dashboard's
            // standby-on-brew effect) from an EXPLICIT user-initiated request.
            // Absent/false (every physical button, web Standby button, HomeKit,
            // and every non-standby request) = explicit — today's behavior is
            // bit-for-bit unchanged. true = automatic — a STANDBY target then
            // rides the settle window instead of stopping instantly.
            bool automatic = doc["auto"].is<bool>() && doc["auto"].as<bool>();
            if (newMode == MODE_GRIND && !controller->isGrindAvailable())
                return;
            if (newMode == MODE_MANUAL && !controller->isManualAvailable())
                return;
            // PRO-421: an explicit Standby wins over a stale, near-immediate
            // re-assert of a non-Standby mode. When Stop-Steam is pressed while
            // auto-steam is enabled, the web dashboard reflexively re-fires
            // `req:change-mode` STEAM ~150 ms after the STANDBY (confirmed live);
            // the firmware is the authoritative layer and must reject that stale
            // re-assert so a single press lands in Standby and stays. STANDBY
            // itself is never suppressed. A deliberate re-entry outside the short
            // guard window still works. Checked BEFORE deactivate() so a
            // suppressed request tears nothing down. Mirrors PRO-391's principle
            // (a stale assertion must not override an explicit Standby) for the
            // WebUIPlugin mode-change path. See StandbyReassertPolicy.h.
            const unsigned long msSinceStandby =
                sawExplicitStandby ? (millis() - lastExplicitStandbyMs) : STANDBY_REASSERT_GUARD_MS;
            if (shouldSuppressStandbyReassert(newMode, msSinceStandby)) {
                return;
            }
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
            // PRO-265: an EXPLICIT STANDBY is a user stop and must NEVER defer — it
            // bypasses the settle window entirely and stops immediately, mirroring
            // Controller::activateStandby() and the physical STANDBY button (which
            // are not gated). PRO-587: an AUTOMATIC STANDBY (standby-on-brew, the
            // `auto` flag above) DOES defer — it rides the same settle window as
            // auto-steam so post-shot drips reach the yield. Non-standby targets
            // (auto-steam MODE_STEAM, grind, manual) keep PRO-261's settle behavior
            // regardless of the flag.
            if (shouldDeferModeChange(newMode, ShotHistory.isExtendedRecording(), automatic)) {
                // Latch target before raising the flag so loop() never reads a stale
                // target for a freshly-armed deferral (volatile handoff, see header).
                // No pending-auto companion flag is needed: a STANDBY target only
                // ever reaches here when automatic == true (an explicit STANDBY
                // never defers), so loop()'s drain re-check treats any armed target
                // as deferrable-while-recording (see WebUIPlugin::loop, PRO-587).
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
                // PRO-421: record when an explicit STANDBY landed so an immediate
                // stale non-STANDBY re-assert (see the guard above) is rejected. An
                // AUTOMATIC standby-on-brew STANDBY never reaches this branch while a
                // settle window is open (it defers); if it lands here (no window) it
                // is still a genuine stop, so treat it as an explicit standby for
                // the reassert guard — this preserves PRO-421 exactly.
                if (newMode == MODE_STANDBY) {
                    lastExplicitStandbyMs = millis();
                    sawExplicitStandby = true;
                }
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
    } else if (msgType == "req:standby-on-brew:set") {
        // Device-authoritative standby-on-brew toggle (PRO-545). Persisted via
        // Settings and rebroadcast to all clients in the next evt:status as "sb".
        // Mirrors req:autosteam:set exactly. Mutual exclusion with auto-steam is
        // enforced client-side (the button disables while auto-steam is on); the
        // stored value is preserved here so it resumes when auto-steam is cleared.
        JsonVariantConst enabledValue = doc["enabled"];
        if (enabledValue.is<bool>()) {
            controller->getSettings().setStandbyOnBrewEnabled(enabledValue.as<bool>());
        } else if (enabledValue.is<int>()) {
            controller->getSettings().setStandbyOnBrewEnabled(enabledValue.as<int>() != 0);
        } else {
            ESP_LOGW("WebUIPlugin", "req:standby-on-brew:set ignored: missing or invalid 'enabled'");
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
    } else if (msgType == "req:manual-grind:set") {
        // Device-authoritative manual grinder-dial setting (PRO-603). Validated
        // and clamped by Settings::setManualGrindSetting, rebroadcast as "mg" in
        // evt:status. Unlike dose, 0 is a valid value ("not set"), so accept
        // [0, 100]; reject only non-finite or out-of-range.
        JsonVariantConst gValue = doc["value"];
        if (!gValue.isNull() && gValue.is<float>()) {
            const double value = gValue.as<double>();
            if (!std::isfinite(value) || value < 0.0 || value > 100.0) {
                ESP_LOGW("WebUIPlugin", "req:manual-grind:set ignored: 'value' out of range");
            } else {
                controller->getSettings().setManualGrindSetting(value);
            }
        } else {
            ESP_LOGW("WebUIPlugin", "req:manual-grind:set ignored: missing or invalid 'value'");
        }
    } else if (msgType == "req:beans:select") {
        String beanName = doc["name"].is<String>() ? doc["name"].as<String>() : String("");
        controller->getSettings().setSelectedBean(beanName);
        pluginManager->trigger(EventIds::BEANS_SELECTED, "name", beanName);
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
    }
}

// Resolve a stored OTA channel string to the GitHub release URL fragment.
// "latest"      -> "latest" (resolves to most recent non-prerelease)
// "beta"        -> "tag/beta" (moving tag tracking the master branch)
// "nightly"     -> "tag/nightly"
// "tag:<semver>" (validated against STABLE_VERSIONS allow-list) -> "tag/<semver>"
// anything else -> "latest"
static String resolveReleaseUrl(const String &channel) {
    return String(resolveOtaReleaseUrl(channel.c_str(), RELEASE_URL.c_str(), STABLE_VERSIONS, STABLE_VERSIONS_COUNT).c_str());
}

// Normalize an incoming channel to the value we persist in settings.
// "beta" and "nightly" are accepted moving-tag channels; "tag:<semver>" is
// validated against the STABLE_VERSIONS allow-list. Unknown values fall back
// to "latest" so a malformed websocket payload can never poison the stored
// setting.
static String normalizeChannel(const String &channel) {
    return String(normalizeOtaChannel(channel.c_str(), STABLE_VERSIONS, STABLE_VERSIONS_COUNT).c_str());
}

void WebUIPlugin::handleOTASettings(uint32_t clientId, JsonDocument &request) {
    // `lastUpdateCheck` is intentionally exempt from the loop-task-ownership model
    // the rest of this handler follows (CAR-178/CAR-377): it is a single
    // word-aligned `unsigned long` whose write is atomic on ESP32, and 0 is a
    // force-recheck sentinel where a stale read merely delays the next check by one
    // interval. So it is safe to set directly here rather than via a deferred flag.
    lastUpdateCheck = 0;
    // PRO-562: reset the defer-log gate alongside the force-recheck sentinel so the
    // first deferred-check log after this recheck isn't suppressed by a stale
    // in-cooldown timestamp. This mirrors the reset in loop() at ~L806, but note
    // that L806 runs on the loop task whereas this reset runs on the AsyncTCP/relay
    // task — see the atomic-bool-write exemption on `otaDeferLogged` in the header:
    // a single `bool` write doesn't tear on ESP32 and a stale read is at worst a
    // cosmetic extra (un)suppressed log line, so the cross-task write is safe.
    otaDeferLogged = false;
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
                const OtaDeferredStringIntent posted = postOtaDeferredIntent(url.c_str());
                pendingReleaseUrl = posted.payload.c_str();
                pendingReleaseUrlChange = posted.pending;
                xSemaphoreGive(otaIntentMutex);
            } else {
                // Should be effectively impossible — the lock is only ever held
                // for three trivial assignments on the loop-task drain side — but
                // never drop a channel change silently. Raise the flag anyway: the
                // loop-task drain re-resolves the URL from the persisted channel
                // when no explicit URL was handed off (emptyHandoff), so the new
                // channel still reaches `ota` on the next loop iteration.
                const OtaDeferredStringIntent posted = postOtaDeferredIntentFlagOnly();
                pendingReleaseUrlChange = posted.pending;
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
        const OtaDeferredStringIntent posted = postOtaDeferredIntent(component.c_str());
        pendingUpdateComponent = posted.payload.c_str();
        pendingOtaStart = posted.pending;
        xSemaphoreGive(otaIntentMutex);
    } else {
        // Effectively impossible (the lock only ever wraps a few trivial
        // assignments), but never drop a start request silently. Raise the flag
        // anyway; loop() finds an empty pendingUpdateComponent and defaults to a
        // full update (both display and controller) — the safe superset.
        const OtaDeferredStringIntent posted = postOtaDeferredIntentFlagOnly();
        pendingOtaStart = posted.pending;
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
            pluginManager->trigger(EventIds::BEANS_SELECTED, "name", "");
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
            // PRO-424: mirror the bean delete-cleanup. Grinders are a recorded
            // history with no hard-delete endpoint, so a save/sync is the only
            // point a previously-selected grinder can disappear (evicted by the
            // cap, or replaced by a batch sync that omits it). If the currently
            // selected grinder is no longer present, clear the selection and
            // emit the event so the web clients converge.
            const auto grinders = grinderManager->listGrinders();
            const String selected = controller->getSettings().getSelectedGrinder();
            if (!selected.isEmpty()) {
                bool stillPresent = std::find(grinders.begin(), grinders.end(), selected) != grinders.end();
                if (!stillPresent) {
                    controller->getSettings().setSelectedGrinder("");
                    pluginManager->trigger(EventIds::GRINDERS_SELECTED, "name", String(""));
                }
            }
            auto arr = response["grinders"].to<JsonArray>();
            for (const auto &grinder : grinders) {
                arr.add(grinder);
            }
        }
    } else if (type == "req:grinders:select") {
        // PRO-424: firmware-authoritative selected grinder, mirroring
        // req:beans:select. Persist to NVS and broadcast so every client
        // (and the status payload's "gr" key) reflects the new selection.
        String grinderName = request["name"].is<String>() ? request["name"].as<String>() : String("");
        controller->getSettings().setSelectedGrinder(grinderName);
        pluginManager->trigger(EventIds::GRINDERS_SELECTED, "name", grinderName);
        response["name"] = grinderName;
    }

    sendResponse(clientId, response);
}

void WebUIPlugin::handleSettingsProvisioning(AsyncWebServerRequest *request) {
    const bool authenticated = isHttpAuthenticated(request);
    const bool hasSsid = request->hasArg("wifiSsid");
    const bool hasPassword = request->hasArg("wifiPassword");
    const bool hasMdnsName = request->hasArg("mdnsName");
    const bool complete =
        request->hasArg("completeLocalAuthProvisioning") && request->arg("completeLocalAuthProvisioning") == "1";
    const bool restart = request->hasArg("restart") && request->arg("restart") == "1";
    if (!localAuthMayProvisionInAp(apMode, authenticated, hasSsid, hasPassword, hasMdnsName, complete, restart)) {
        sendUnauthorized(request);
        return;
    }

    const String mdnsName = request->arg("mdnsName");
    if (!isValidMdnsName(mdnsName.c_str(), mdnsName.length())) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->setCode(400);
        addCorsHeaders(response);
        response->print("{\"error\":\"Invalid mDNS hostname\"}");
        request->send(response);
        return;
    }

    // Deliberately do not call handleSettings(): omitted checkbox fields there
    // mean false. This narrow endpoint mutates only AP recovery data atomically.
    controller->getSettings().batchUpdate([request, mdnsName](Settings *settings) {
        settings->setWifiSsid(request->arg("wifiSsid"));
        settings->setWifiPassword(request->arg("wifiPassword"));
        settings->setMdnsName(mdnsName);
        settings->setLocalAuthProvisioned(true);
        settings->save(true);
    });

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    addCorsHeaders(response);
    response->print("{\"success\":true}");
    request->send(response);

    if (restart)
        ESP.restart();
}

void WebUIPlugin::handleSettings(AsyncWebServerRequest *request) {
    if (!isHttpAuthenticated(request)) {
        sendUnauthorized(request);
        return;
    }
    if (request->method() == HTTP_POST) {
        if (request->hasArg("mdnsName")) {
            const String mdnsName = request->arg("mdnsName");
            if (!isValidMdnsName(mdnsName.c_str(), mdnsName.length())) {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                response->setCode(400);
                addCorsHeaders(response);
                response->print("{\"error\":\"Invalid mDNS hostname\"}");
                request->send(response);
                return;
            }
        }

        // Validate every supplied scalar before mutating Settings. This is a
        // transaction boundary: a bad field returns 400 and no partial update is
        // persisted. String/toInt() coercion below is safe only after this pass.
        static const char *const validatedFields[] = {"startupMode",
                                                      "targetSteamTemp",
                                                      "targetWaterTemp",
                                                      "temperatureOffset",
                                                      "pressureScaling",
                                                      "startupFillTime",
                                                      "steamFillTime",
                                                      "smartGrindMode",
                                                      "haPort",
                                                      "brewDelay",
                                                      "grindDelay",
                                                      "standbyTimeout",
                                                      "mainBrightness",
                                                      "standbyBrightness",
                                                      "standbyBrightnessTimeout",
                                                      "steamPumpPercentage",
                                                      "steamPumpCutoff",
                                                      "themeMode",
                                                      "sunriseR",
                                                      "sunriseG",
                                                      "sunriseB",
                                                      "sunriseW",
                                                      "sunriseExtBrightness",
                                                      "emptyTankDistance",
                                                      "fullTankDistance",
                                                      "altRelayFunction",
                                                      "autowakeupSchedules",
                                                      "flushDuration"};
        strict_validation::Fields fields;
        for (const char *name : validatedFields) {
            if (request->hasArg(name))
                fields.push_back({name, request->arg(name).c_str()});
        }
        strict_validation::Error validationError;
        if (!strict_validation::validateSettings(fields, validationError)) {
            JsonDocument error;
            error["error"] = "Invalid settings";
            error["field"] = validationError.field.c_str();
            error["detail"] = validationError.message.c_str();
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->setCode(400);
            addCorsHeaders(response);
            serializeJson(error, *response);
            request->send(response);
            return;
        }
        controller->getSettings().batchUpdate([this, request](Settings *settings) {
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
            // online — the EventIds::SETTINGS_CHANGED trigger below arms the tee without
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
            if (apMode && request->hasArg("completeLocalAuthProvisioning")) {
                settings->setLocalAuthProvisioned(true);
            }
            settings->save(true);
        });
        pluginManager->trigger(EventIds::SETTINGS_CHANGED);
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
    // AP fallback is the physical-presence setup channel. It is the only place
    // the bootstrap token is returned; normal LAN responses never disclose it.
    if (apMode)
        doc["localAdminToken"] = settings.getLocalAdminToken();
    else
        doc["localAdminToken"] = kSecretSentinel;
    doc["localAuthRecovery"] = apMode && !settings.isLocalAuthProvisioned();
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

    const String uuid = request->arg("uuid");
    JsonDocument doc;
    if (uuid.isEmpty()) {
        doc["success"] = false;
        doc["error"] = "Missing or empty UUID";
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->setCode(400);
        addCorsHeaders(response);
        serializeJson(doc, *response);
        request->send(response);
        return;
    }

    const bool accepted = BLEScales.connect(uuid.c_str());
    doc["success"] = accepted;
    if (accepted) {
        doc["accepted"] = true;
        doc["message"] = "Connection attempt accepted";
    } else {
        doc["error"] = "Connection failed";
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    if (!accepted) {
        response->setCode(400);
    }
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
    // PRO-599: authoritative per-component flash eligibility. The web UI used to
    // re-derive this from (channel, installedChannel, status, *UpdateAvailable)
    // and semver ordering, which silently blocked the Update button on a channel
    // switch to an equal/lower version (and whenever installedChannel was absent).
    // The device is the authority on what it will actually flash, so it reports
    // the decision directly here via the same policy the OTA-start path uses
    // (decideOtaFlash / otaComponentFlashEligible in OtaChannelSwitchPolicy.h).
    {
        const String channelStr = settings.getOTAChannel();
        const String installedStr = settings.getInstalledChannel();
        const bool isTag = channelStr.startsWith("tag:");
        // pinnedTag is the substring after "tag:"; ignored by the policy when !isTag.
        const String pinnedTag = isTag ? channelStr.substring(4) : String("");
        const bool selectedEqInstalled = channelStr == installedStr;
        const bool installedEmpty = installedStr.isEmpty();
        const String resolvedVersion = ota->getCurrentVersion();
        const bool resolveFailed = ota->isUpdateCheckFailed();
        doc["displayFlashEligible"] =
            otaComponentFlashEligible(isTag, pinnedTag.c_str(), selectedEqInstalled, installedEmpty, resolvedVersion.c_str(),
                                      resolveFailed, ota->isUpdateAvailable(false));
        doc["controllerFlashEligible"] =
            otaComponentFlashEligible(isTag, pinnedTag.c_str(), selectedEqInstalled, installedEmpty, resolvedVersion.c_str(),
                                      resolveFailed, ota->isUpdateAvailable(true));
    }
    doc["displayVersion"] = BUILD_GIT_VERSION;
    doc["controllerVersion"] = controller->getSystemInfo().version;
    doc["hardware"] = controller->getSystemInfo().hardware;
    doc["channel"] = settings.getOTAChannel();
    // PRO-400 (Issue B / PRO-401): the channel whose head is installed, so the
    // web UI can detect a pending channel switch (selected != installed) and
    // surface the force-flash affordance.
    doc["installedChannel"] = settings.getInstalledChannel();
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
