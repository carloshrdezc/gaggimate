#ifndef WEBUIPLUGIN_H
#define WEBUIPLUGIN_H

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <DNSServer.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "GitHubOTA.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <display/core/Plugin.h>
#include <vector>

constexpr uint32_t RELAY_CLIENT_ID = 0xFFFFFFFE;

constexpr size_t UPDATE_CHECK_INTERVAL = 5 * 60 * 1000;
constexpr size_t CLEANUP_PERIOD = 5 * 1000;
constexpr size_t STATUS_PERIOD = 500;
constexpr size_t DNS_PERIOD = 10;

const String LOCAL_URL = "http://4.4.4.1/";
const String RELEASE_URL = "https://github.com/carloshrdezc/gaggimate/releases/";

// Last 5 stable releases offered in the OTA dropdown — auto-generated at build
// time by scripts/generate_stable_versions.py from the GitHub Releases API of
// `carloshrdezc/gaggimate`. The script is wired in as a PlatformIO pre-script
// in platformio.ini for the `display` and `controller` envs. If the build is
// offline and the header has never been generated, a single-entry fallback is
// emitted so the firmware still compiles. Update RELEASE_URL above and the
// OWNER/REPO constants in the script in lockstep if the fork ever moves.
#include "../../stable_versions.h"

class ProfileManager;
class BeanManager;
class GrinderManager;

class WebUIPlugin : public Plugin {
  public:
    WebUIPlugin();
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    void setupServer();
    void start();
    void stop();
    void addCorsHeaders(AsyncWebServerResponse *response) const;
    void handleOptions(AsyncWebServerRequest *request) const;

    // Cloud relay
    void startRelay();
    void stopRelay();
    void broadcastAll(const String &msg);
    void broadcastRelayMsg(const String &msg); // thread-safe relay-only send
    void sendResponse(uint32_t clientId, JsonDocument &response);
    void processWebSocketMessage(uint32_t clientId, const String &msg);

    // Websocket handlers
    void handleWebSocketData(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
                             size_t len);
    void handleOTASettings(uint32_t clientId, JsonDocument &request);
    void handleOTAStart(uint32_t clientId, JsonDocument &request);
    void handleAutotuneStart(uint32_t clientId, JsonDocument &request);
    void handleBeanRequest(uint32_t clientId, JsonDocument &request);
    void handleGrinderRequest(uint32_t clientId, JsonDocument &request);
    void handleProfileRequest(uint32_t clientId, JsonDocument &request);
    void handleFlushStart(uint32_t clientId, JsonDocument &request);

    // HTTP handlers
    // Serves the web UI from the firmware-embedded, memory-mapped flash blob
    // (catch-all for any path not claimed by an explicit route). The 2-arg
    // overload serves a fixed asset path (used by the favicon/touch-icon
    // handlers to serve the embedded gm.png). [GM-106]
    void serveWebAsset(AsyncWebServerRequest *request);
    void serveWebAsset(AsyncWebServerRequest *request, String path);
    void handleSettings(AsyncWebServerRequest *request);
    void handleBLEScaleList(AsyncWebServerRequest *request);
    void handleBLEScaleScan(AsyncWebServerRequest *request);
    void handleBLEScaleConnect(AsyncWebServerRequest *request);
    void handleBLEScaleInfo(AsyncWebServerRequest *request);
    void updateOTAStatus(const String &version);
    void updateOTAProgress(uint8_t phase, int progress);
    void sendAutotuneResult();

    // Core dump download
    void handleCoreDumpDownload(AsyncWebServerRequest *request);

    // Diagnostic SD log download (PRO-274). Streams a diag log file
    // (/diag/log.txt active, or /diag/log.1 rotated) raw off the SD card.
    // Returns a clean 404 (NOT the SPA fallback) when no card is mounted or
    // the file is absent. `sdPath` is the absolute SD path to serve.
    void handleDiagLogDownload(AsyncWebServerRequest *request, const char *sdPath);

    // Thread-safety contract for `ota` (CAR-178):
    //
    // `GitHubOTA` is NOT thread-safe — it carries mutable internal state
    // (`_release_url`, `_latest_url`, `_latest_version`, `phase`, the HTTPUpdate
    // and WiFiClientSecure members) with no internal locking. Concurrent calls
    // from different FreeRTOS tasks would tear those reads/writes.
    //
    // The contract that makes it safe: **every method call on `ota` happens on
    // the Arduino main loop task only** (`WebUIPlugin::loop()`, and the
    // `controller:ready` handler which Controller::loop() dispatches on that same
    // task). WebSocket / relay handlers (AsyncTCP task, relay task) NEVER touch
    // `ota` directly — instead they post intent via the `pendingReleaseUrl*` /
    // `pendingOtaStatusPush` flags below, which `loop()` drains on the loop task
    // before its own OTA work. This eliminates the concurrency entirely rather
    // than serializing it, so WS handlers never block on a multi-minute
    // `ota->update()` (approach (b) from CAR-178).
    GitHubOTA *ota = nullptr;
    // Deferred-OTA-intent channel (CAR-178). Written by WS/relay-task handlers,
    // drained by loop() on the Arduino main loop task. `otaIntentMutex` guards
    // the non-atomic `pendingReleaseUrl` String; the two bool flags are simple
    // volatile handoffs (a missed-by-one-tick drain is harmless — loop() runs
    // every ~2 ms and re-checks every iteration).
    SemaphoreHandle_t otaIntentMutex = nullptr;
    String pendingReleaseUrl = "";        // guarded by otaIntentMutex
    volatile bool pendingReleaseUrlChange = false;
    volatile bool pendingOtaStatusPush = false;
    // Deferred OTA-start intent (CAR-377). handleOTAStart runs on the AsyncTCP /
    // relay task; it must not write the loop-task-owned `updating` / `updateComponent`
    // directly (a torn read of the non-atomic `updateComponent` String would feed a
    // wrong component selection into ota->update()). Instead it posts the requested
    // component under otaIntentMutex and raises pendingOtaStart; loop() latches both
    // onto the loop task before the update runs, exactly like the release-URL handoff.
    String pendingUpdateComponent = ""; // guarded by otaIntentMutex
    volatile bool pendingOtaStart = false;
    AsyncWebServer server;
    AsyncWebSocket ws;
    WebSocketsClient relayWs;
    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    DNSServer *dnsServer = nullptr;
    BeanManager *beanManager = nullptr;
    GrinderManager *grinderManager = nullptr;
    ProfileManager *profileManager = nullptr;

    // Relay state
    SemaphoreHandle_t relayMutex = nullptr;
    // Serializes the relay lifecycle (startRelay()/stopRelay()). Those two run on
    // different FreeRTOS tasks (the arduino_events WiFi-event task via start()/stop()
    // and the AsyncTCP /api/settings task via handleSettings()) and can genuinely
    // interleave, so the volatile-flag handoff with relayLoopTask is only coherent
    // when the lifecycle itself is mutually excluded by this mutex (CAR-259).
    SemaphoreHandle_t relayLifecycleMutex = nullptr;
    std::vector<String> relayOutBuffer;
    volatile bool relayEnabled = false;
    volatile bool relayConnected = false;
    // Cooperative-shutdown channel for relayLoopTask. stopRelay() sets
    // relayTaskExitRequested and waits for the task to tear down its own
    // WebSocket state and self-delete (nulling relayTaskHandle), rather than
    // calling vTaskDelete on a remote handle mid-relayWs.loop() (CAR-259).
    volatile bool relayTaskExitRequested = false;
    // volatile: this handle is the variable stopRelay()'s bounded spin-wait
    // polls while the relay task (other core) writes it to nullptr just before
    // vTaskDelete(NULL). Without volatile the compiler may hoist/cache the load
    // and never observe the null, spuriously timing out a clean shutdown (CAR-259).
    // NOTE: volatile only prevents a cached/hoisted load of this handle — it does
    // NOT establish cross-core ordering. In particular it does not guarantee the
    // relay task's relayWs.disconnect() is visible before its relayTaskHandle =
    // nullptr store on the observing core. Hardening that ordering (migrating the
    // handle/flags to std::atomic with acquire/release semantics) is tracked as a
    // separate follow-up and is intentionally NOT done here (CAR-259).
    volatile TaskHandle_t relayTaskHandle = nullptr;
    static void relayLoopTask(void *arg);

    unsigned long lastUpdateCheck = 0;
    unsigned long lastStatus = 0;
    unsigned long lastCleanup = 0;
    unsigned long lastDns = 0;
    bool updating = false; // loop-task-owned; set via pendingOtaStart drain (CAR-377)
    bool apMode = false;
    bool serverRunning = false;
    String updateComponent = ""; // loop-task-owned; latched from pendingUpdateComponent (CAR-377)
    float currentBluetoothWeight = 0.0f;

    // Deferred mode-change intent (PRO-261). A `req:change-mode` arrives on the
    // AsyncTCP (`handleWebSocketData`) or relay (`relayLoopTask`) task, never the
    // Arduino main loop task. The display's own auto-steam path
    // (DefaultUI::loop / `pendingAutoSteam`) holds the brew->steam transition
    // until ShotHistory.isExtendedRecording() clears so the BLE scale can settle
    // and post-stop drips land in the recorded yield (PRO-223 / PRO-248 /
    // PRO-232). The web `req:change-mode` path bypassed that gate and called
    // clear()/setMode() immediately, aborting the settle window and producing a
    // short yield.
    //
    // To make the gate firmware-authoritative (protecting web + relay + any
    // future remote, with no wire-contract change), the handler ends the active
    // process (deactivate(), which opens the settle window if a healthy BLE scale
    // was the source) and, when isExtendedRecording() is still true, posts the
    // requested mode here instead of clearing/switching. loop() — on the main
    // task — drains this every ~2 ms: it holds while the window is open, then
    // applies clear()+setMode() once it closes, exactly mirroring the display
    // gate (including the getMode()==MODE_BREW guard so a user who navigated away
    // to standby in the meantime does not get an unwanted steam transition).
    // Non-scale / time-based shots open no window, so isExtendedRecording() is
    // already false and the handler applies the change inline with no added
    // latency (this deferral is never armed for them).
    //
    // volatile handoff (no mutex needed): both fields are written only by the
    // WS/relay handler under a single store each and read/cleared only by loop().
    // The target is a single-word scalar latched before the flag is raised, so
    // there is no tearing. On a dual-core ESP32 bare volatile prevents compiler
    // reordering but does not establish strict cross-core store ordering (see the
    // CAR-259 caveat above), so loop() could in principle observe the flag a tick
    // before the target store lands — but that one-tick stale read is harmless and
    // self-correcting: the intent is re-posted on every arm and drained every ~2 ms,
    // making such a window astronomically unlikely and transient regardless. A mutex
    // is unnecessary because this is a single word (unlike the OTA String payload
    // above, which is non-atomic and does need otaIntentMutex).
    volatile uint8_t pendingModeChangeTarget = 0;
    volatile bool pendingModeChange = false;
};

#endif // WEBUIPLUGIN_H
