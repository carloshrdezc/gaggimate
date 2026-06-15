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
    bool updating = false;
    bool apMode = false;
    bool serverRunning = false;
    String updateComponent = "";
    float currentBluetoothWeight = 0.0f;
};

#endif // WEBUIPLUGIN_H
