#include "WebUIPlugin.h"
#include <DNSServer.h>
#include <SPIFFS.h>
#include <display/core/Controller.h>
#include <display/core/GrinderManager.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/core/utils.h>
#include <display/models/profile.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <SD_MMC.h>
#include <algorithm>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ShotHistoryPlugin.h>
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
        if (colonIdx < 0) { host = hostPort; port = 443; }
        else { host = hostPort.substring(0, colonIdx); port = (uint16_t)hostPort.substring(colonIdx + 1).toInt(); }
        return true;
    }
    if (url.startsWith("ws://")) {
        useSSL = false;
        String rest = url.substring(5);
        int slashIdx = rest.indexOf('/');
        String hostPort = (slashIdx < 0) ? rest : rest.substring(0, slashIdx);
        basePath = (slashIdx < 0) ? String("/") : rest.substring(slashIdx);
        int colonIdx = hostPort.indexOf(':');
        if (colonIdx < 0) { host = hostPort; port = 80; }
        else { host = hostPort.substring(0, colonIdx); port = (uint16_t)hostPort.substring(colonIdx + 1).toInt(); }
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
        BUILD_GIT_VERSION, controller->getSystemInfo().version,
        resolveReleaseUrl(controller->getSettings().getOTAChannel()),
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

    setupServer();
}

void WebUIPlugin::relayLoopTask(void *arg) {
    auto *plugin = static_cast<WebUIPlugin *>(arg);
    while (true) {
        // Cooperative shutdown: stopRelay() requests exit, and the teardown
        // runs here on the task that owns the WebSocket allocations / mbedTLS
        // state, never via vTaskDelete on a remote handle mid-loop (CAR-259).
        if (plugin->relayTaskExitRequested) {
            plugin->relayWs.disconnect();
            plugin->relayConnected = false;
            // Publish NULL before self-deleting so stopRelay()'s wait observes
            // the task is gone. After vTaskDelete(NULL) this task never runs
            // again, so no further access to plugin state occurs.
            plugin->relayTaskHandle = nullptr;
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

void WebUIPlugin::loop() {
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
            const bool match = resolved == pinned ||
                               (pinned.startsWith("v") && resolved == pinned.substring(1)) ||
                               (resolved.startsWith("v") && resolved.substring(1) == pinned);
            if (!match) {
                ESP_LOGE("WebUIPlugin",
                         "Refusing forced OTA: pinned tag %s but resolved %s",
                         pinned.c_str(), resolved.c_str());
                tagResolved = false;
            }
        }
        bool updateSucceeded = false;
        if (tagResolved) {
            updateSucceeded =
                ota->update(updateComponent != "display", updateComponent != "controller", force);
        }
        pluginManager->trigger("ota:update:end");
        updating = false;
        if (!updateSucceeded) {
            updateOTAStatus(tagResolved ? "Update failed" : "Update failed (tag not resolved)");
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
    if (lastUpdateCheck == 0 || now - lastUpdateCheck > UPDATE_CHECK_INTERVAL) {
        ota->checkForUpdates();
        pluginManager->trigger("ota:update:status", "value", ota->isUpdateAvailable());
        lastUpdateCheck = now;
        updateOTAStatus(ota->getCurrentVersion());
    }
    if (now - lastStatus > STATUS_PERIOD && (!ws.getClients().empty() || relayConnected)) {
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

        bool bleConnected = BLEScales.isConnected();
        // Add Bluetooth scale weight information
        doc["cw"] = bleConnected ? this->currentBluetoothWeight : 0; // current bluetooth weight
        doc["bc"] = bleConnected;                                    // bluetooth scale connected status

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
                const bool isVolumetric = proc.target == ProcessTarget::VOLUMETRIC && proc.hasVolumetricTarget &&
                                          controller->isVolumetricAvailable();
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
        ws.cleanupClients();
    }
    if (now - lastDns > DNS_PERIOD && dnsServer != nullptr) {
        lastDns = now;
        dnsServer->processNextRequest();
    }
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
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) { handleBLEScaleList(request); });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) { handleBLEScaleConnect(request); });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) { handleBLEScaleScan(request); });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) { handleBLEScaleInfo(request); });
    FS *fs = &SPIFFS;
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
    server.on("/test", [](AsyncWebServerRequest *request) {
        ESP_LOGI("WebUI", "TEST endpoint hit!");
        request->send(200, "text/plain", "ESP32 server is alive!");
    });
    // Handle missing favicon/icons explicitly before serveStatic
    server.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(SPIFFS, "/w/gm.png", "image/png"); });
    server.on("/apple-touch-icon.png", [](AsyncWebServerRequest *request) { request->send(SPIFFS, "/w/gm.png", "image/png"); });
    server.on("/apple-touch-icon-precomposed.png", [](AsyncWebServerRequest *request) { request->send(SPIFFS, "/w/gm.png", "image/png"); });
    // Vite emits content-hashed asset names. Cache them aggressively so route
    // navigation does not repeatedly hit the ESP32 for immutable chunks/fonts.
    server.serveStatic("/assets/", SPIFFS, "/w/assets/").setCacheControl("public, max-age=31536000, immutable");
    server.serveStatic("/fonts/", SPIFFS, "/w/fonts/").setCacheControl("public, max-age=31536000, immutable");
    // onNotFound must be registered BEFORE serveStatic so it catches unmatched paths
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/w/index.html");
    });
    server.serveStatic("/", SPIFFS, "/w").setDefaultFile("index.html").setCacheControl("max-age=0");
    ws.onEvent(
        [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            if (type == WS_EVT_CONNECT) {
                client->setCloseClientOnQueueFull(true);
                ESP_LOGI("WebUIPlugin", "WebSocket client connected (%d open connections)", server->getClients().size());
            } else if (type == WS_EVT_DISCONNECT) {
                ESP_LOGI("WebUIPlugin", "WebSocket client disconnected (%d open connections)", server->getClients().size());
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
    serverRunning = true;
    startRelay();
}

void WebUIPlugin::stop() {
    stopRelay();
    if (!serverRunning)
        return;
    server.end();
    ws.closeAll();
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

// RAII guard for the relay lifecycle mutex. Takes the mutex on construction
// (if it exists) and gives it back on destruction, so every return path out of
// startRelay()/stopRelay() — early guards, the deferred-start/timeout returns,
// the SSL heap-guard, the OOM path, and the normal returns — releases the lock
// without a hand-audited give at each site (CAR-259).
namespace {
struct RelayLifecycleLock {
    SemaphoreHandle_t handle;
    explicit RelayLifecycleLock(SemaphoreHandle_t h) : handle(h) {
        if (handle != nullptr) {
            xSemaphoreTake(handle, portMAX_DELAY);
        }
    }
    ~RelayLifecycleLock() {
        if (handle != nullptr) {
            xSemaphoreGive(handle);
        }
    }
    RelayLifecycleLock(const RelayLifecycleLock &) = delete;
    RelayLifecycleLock &operator=(const RelayLifecycleLock &) = delete;
};
} // namespace

void WebUIPlugin::startRelay() {
    // Caller-context: startRelay()/stopRelay() are invoked from two different
    // FreeRTOS tasks — start()/stop() run inline on the arduino_events WiFi-event
    // task (controller:wifi:connect/disconnect), while handleSettings() runs on the
    // AsyncTCP /api/settings task and calls stopRelay()+startRelay() back-to-back.
    // A WiFi (dis)connect can therefore genuinely interleave with a cloud-relay
    // settings toggle. They are now serialized by relayLifecycleMutex (taken at the
    // top of both functions, released on every return path) so the volatile-flag
    // handoff with relayLoopTask stays coherent and two starts can never both
    // observe relayTaskHandle==nullptr and orphan a live task (CAR-259). A plain
    // (non-recursive) mutex is correct: start() calls stop() then startRelay()
    // sequentially (not nested), and handleSettings() calls them sequentially too —
    // neither function calls the other while holding the lock.
    RelayLifecycleLock lock(relayLifecycleMutex);
    const String &relayUrl = controller->getSettings().getCloudRelayUrl();
    const String &relayToken = controller->getSettings().getCloudRelayToken();
    if (relayUrl.isEmpty() || relayToken.isEmpty() || !controller->getSettings().isCloudRelayEnabled()) return;

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
    if (relayTaskHandle != nullptr) {
        constexpr TickType_t drainInterval = pdMS_TO_TICKS(10);
        constexpr int maxDrainPolls = 50; // ~500 ms
        int drainPolls = 0;
        while (relayTaskHandle != nullptr && drainPolls < maxDrainPolls) {
            vTaskDelay(drainInterval);
            ++drainPolls;
        }
        if (relayTaskHandle != nullptr) {
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

    String path = (basePath.isEmpty() || basePath == "/")
        ? "/connect?token=" + relayToken + "&role=device"
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

    // SSL heap usage can reach 50 KB; bail early rather than destabilize the device.
    if (useSSL && esp_get_free_heap_size() < 60000) {
        ESP_LOGW("WebUIPlugin", "Insufficient heap (%lu B) for SSL relay — skipping", esp_get_free_heap_size());
        return;
    }

    relayWs.setReconnectInterval(5000);
    if (useSSL) {
        relayWs.beginSSL(host.c_str(), port, path.c_str());
    } else {
        relayWs.begin(host.c_str(), port, path.c_str());
    }

    // relayTaskHandle is guaranteed null here (the live-task case returned above).
    relayTaskExitRequested = false; // fresh task must not see a stale exit request
    // xTaskCreatePinnedToCore wants a non-volatile TaskHandle_t*; create into a
    // local, then publish to the volatile member.
    TaskHandle_t createdHandle = nullptr;
    BaseType_t created = xTaskCreatePinnedToCore(relayLoopTask, "WebUIRelay", 16384, this, 1, &createdHandle, 0);
    if (created != pdPASS) {
        ESP_LOGE("WebUIPlugin", "Failed to create relay task (OOM)");
        relayWs.disconnect();
        return;
    }
    relayTaskHandle = createdHandle;

    relayEnabled = true;
    ESP_LOGI("WebUIPlugin", "Relay client started → %s:%d%s (free heap: %lu B)", host.c_str(), port, path.c_str(), esp_get_free_heap_size());
}

void WebUIPlugin::stopRelay() {
    // Serialized with startRelay() via relayLifecycleMutex (see startRelay()
    // for the cross-task rationale, CAR-259). The lock is held across the bounded
    // ~500 ms spin-wait below; that is acceptable because the wait uses vTaskDelay
    // (yields the CPU) and is strictly bounded.
    RelayLifecycleLock lock(relayLifecycleMutex);
    if (!relayEnabled) return;
    relayEnabled = false;
    relayConnected = false;
    if (relayTaskHandle != nullptr) {
        // Cooperative shutdown (CAR-259): ask the task to tear down its own
        // WebSocket state and self-delete. We must NOT vTaskDelete a remote
        // handle while it may be inside relayWs.loop() (WebSocketsClient /
        // AsyncTCP / mbedTLS allocations), which leaks heap or corrupts the
        // allocator. The task nulls relayTaskHandle just before vTaskDelete(NULL).
        relayTaskExitRequested = true;
        // Bound the wait so a wedged task can never hang the caller (this runs
        // on the WiFi-event / AsyncTCP web-server task). Loop cadence is 10 ms;
        // a single relayWs.loop() with an SSL handshake or large frame in flight
        // can exceed that, so allow generous slack before giving up.
        constexpr TickType_t pollInterval = pdMS_TO_TICKS(10);
        constexpr int maxPolls = 50; // ~500 ms
        int polls = 0;
        while (relayTaskHandle != nullptr && polls < maxPolls) {
            vTaskDelay(pollInterval);
            ++polls;
        }
        if (relayTaskHandle != nullptr) {
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
    relayTaskExitRequested = false;
}

void WebUIPlugin::broadcastAll(const String &msg) {
    ws.textAll(msg);
    broadcastRelayMsg(msg);
}

void WebUIPlugin::broadcastRelayMsg(const String &msg) {
    if (!relayEnabled || relayMutex == nullptr) return;
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
            ws.text(clientId, buffer);
        }
    }
    // Always forward responses to relay so remote browsers receive them
    broadcastRelayMsg(responseStr);
}

void WebUIPlugin::processWebSocketMessage(uint32_t clientId, const String &msg) {
    ESP_LOGV("WebUIPlugin", "Processing message from %s: %.*s",
             clientId == RELAY_CLIENT_ID ? "relay" : "local",
             (int)msg.length(), msg.c_str());
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg.c_str());
    if (err) return;

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
        float pressure = pressureValue.is<float>() || pressureValue.is<int>() ? pressureValue.as<float>()
                                                                              : controller->getManualPressure();
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
            controller->deactivate();
            controller->clear();
            controller->setMode(newMode);
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
    } else if (msgType == "req:beans:select") {
        String beanName = doc["name"].is<String>() ? doc["name"].as<String>() : String("");
        controller->getSettings().setSelectedBean(beanName);
        pluginManager->trigger("beans:selected", "name", beanName);
    } else if (msgType == "req:history:rebuild") {
        JsonDocument resp;
        resp["tp"] = "res:history:rebuild";
        if (doc["rid"].is<const char *>()) resp["rid"] = doc["rid"];
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
// "nightly"     -> "tag/nightly"
// "tag:<semver>" (validated against STABLE_VERSIONS allow-list) -> "tag/<semver>"
// anything else -> "latest"
static String resolveReleaseUrl(const String &channel) {
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
// Unknown values fall back to "latest" so a malformed websocket payload
// can never poison the stored setting.
static String normalizeChannel(const String &channel) {
    if (channel == "nightly") return "nightly";
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
    lastUpdateCheck = 0;
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
    updating = true;
    if (request["cp"].is<String>()) {
        updateComponent = request["cp"].as<String>();
    } else {
        updateComponent = "";
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
            settings->setAutoWakeupEnabled(request->hasArg("autowakeupEnabled") && request->arg("autowakeupEnabled").length() > 0);
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
    // SPIFFS usage metrics
    {
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
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
