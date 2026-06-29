#ifndef WEBUIPLUGIN_H
#define WEBUIPLUGIN_H

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <DNSServer.h>
#include <WebSocketsClient.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../core/constants.h"
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
    // INVARIANT (PRO-313): every access to `ws` that walks or mutates its
    // internal client list MUST hold `wsMutex` for the duration of the call.
    // AsyncWebSocket (ESP32Async/ESPAsyncWebServer v3.9.1) is NOT thread-safe:
    // it keeps a std::list<AsyncWebSocketClient> with no internal locking, and
    // this firmware touches it from two FreeRTOS tasks that run on different
    // cores (loopTask on the Arduino core; the AsyncTCP task pinned to core 0
    // via CONFIG_ASYNC_TCP_RUNNING_CORE=0). loopTask walks the list in
    // textAll() / cleanupClients() / getClients(); the AsyncTCP task mutates it
    // (emplace_back on connect, erase on disconnect) from inside the onEvent
    // callback path. Concurrent walk+mutate corrupts the list and reboots the
    // device (LoadProhibited). `wsMutex` serializes the two sides. The lock is
    // taken on BOTH sides or it does nothing: every loopTask broadcast/cleanup
    // site AND the onEvent connect/disconnect reads take it (the onEvent data
    // path locks inside sendResponse() instead). Build the JSON payload BEFORE
    // taking the lock — keep the critical section to just the ws call. See
    // WsClientsLock (the RAII guard) and broadcastAll().
    AsyncWebSocket ws;
    // Serializes all `ws` client-list access across loopTask and the AsyncTCP
    // task (PRO-313). Created in setup() before setupServer() registers the
    // onEvent handler, so it exists before any client can connect. Plain
    // (non-recursive) mutex: critical sections are a single ws call and never
    // nest, and ws access never happens while holding relayMutex (broadcastAll
    // / sendResponse release wsMutex before calling broadcastRelayMsg), so
    // there is no lock-ordering inversion.
    SemaphoreHandle_t wsMutex = nullptr;
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
    // interleave, so the atomic-flag handoff with relayLoopTask is only coherent
    // when the lifecycle itself is mutually excluded by this mutex (CAR-259).
    SemaphoreHandle_t relayLifecycleMutex = nullptr;
    std::vector<String> relayOutBuffer;
    volatile bool relayEnabled = false;
    volatile bool relayConnected = false;
    // Cooperative-shutdown channel for relayLoopTask. stopRelay() sets
    // relayTaskExitRequested and waits for the task to tear down its own
    // WebSocket state and self-delete (nulling relayTaskHandle), rather than
    // calling vTaskDelete on a remote handle mid-relayWs.loop() (CAR-259).
    // std::atomic (release on set / acquire on the relay task's top-of-loop
    // read) so the request is published without relying on plain-volatile,
    // which never guaranteed cross-core ordering (PRO-35).
    std::atomic<bool> relayTaskExitRequested{false};
    // std::atomic handle: stopRelay()'s bounded spin-wait reads it with acquire
    // while the relay task (other core) stores nullptr with release just before
    // vTaskDelete(NULL). The release store / acquire load pair closes the
    // cross-core ordering gap the old plain-volatile handle left open: it now
    // guarantees the relay task's relayWs.disconnect() / relayConnected teardown
    // happens-before the observing core sees relayTaskHandle == nullptr, so a
    // clean shutdown is never observed half-torn-down (PRO-35, was CAR-259).
    std::atomic<TaskHandle_t> relayTaskHandle{nullptr};
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
    //
    // The default initializer 0 deliberately equals MODE_STANDBY (see
    // constants.h), so an un-armed/default target reads as standby. This relies
    // on MODE_STANDBY == 0; redefining MODE_STANDBY nonzero would silently change
    // the default-target meaning.
    volatile uint8_t pendingModeChangeTarget = 0;
    volatile bool pendingModeChange = false;
};

// PRO-286: enforce at compile time the invariant the comment above documents — the
// pendingModeChangeTarget default-0 initializer only reads as "standby" if MODE_STANDBY == 0.
static_assert(MODE_STANDBY == 0,
              "PRO-286: pendingModeChangeTarget default-0 init assumes MODE_STANDBY==0 (see WebUIPlugin.h comment / constants.h)");

#endif // WEBUIPLUGIN_H
