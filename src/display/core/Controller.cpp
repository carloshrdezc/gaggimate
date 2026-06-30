#include "Controller.h"
#include "ArduinoJson.h"
#ifndef GAGGIMATE_SIM
// PRO-243: nanopb SystemInfo decode for the INFO characteristic (real firmware
// only; the sim keeps the legacy JSON path — see setupInfos()).
#include "comms.pb.h"
#include "pb_decode.h"
#endif
#ifndef GAGGIMATE_SIM
// PRO-330: esp_wifi_set_ps() — enforce WiFi modem-sleep before BLE controller
// init so WiFi/BLE software coexistence can enable (device-only; the sim stubs
// BLE and has no coexistence path).
#include <esp_wifi.h>
#endif
#include <SD_MMC.h>
#include <LittleFS.h>
#include <ctime>
// PRO-335: esp_sntp.h for sntp_set_sync_mode(). The sim provides a host shim
// (sim/platform/esp_sntp.h) so this links in [env:display-sim] too.
#include "esp_sntp.h"
#include <display/config.h>
#include <display/config/features.h>
#include <display/core/StandbyTransitionPolicy.h>
#include <display/core/WiFiFallbackPolicy.h>
#include <display/core/constants.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/core/process/ManualProcess.h>
#include <display/core/process/PumpProcess.h>
#include <display/core/process/SteamProcess.h>
#include <display/core/static_profiles.h>
#include <display/core/zones.h>
#include <display/plugins/AutoWakeupPlugin.h>
#if GAGGIMATE_ENABLE_BLE_SCALE
#include <display/plugins/BLEScalePlugin.h>
#endif
#include <display/plugins/BoilerFillPlugin.h>
#if GAGGIMATE_ENABLE_HOMEKIT
#include <display/plugins/HomekitPlugin.h>
#endif
#include <display/plugins/LedControlPlugin.h>
#if GAGGIMATE_ENABLE_MQTT
#include <display/plugins/MQTTPlugin.h>
#endif
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/plugins/SmartGrindPlugin.h>
#if GAGGIMATE_ENABLE_WEBUI
#include <display/plugins/WebUIPlugin.h>
#endif
#ifndef GAGGIMATE_SIM // mDNS is device-only (the sim WiFi shim has no real mDNS)
#include <display/plugins/mDNSPlugin.h>
#endif
#ifndef GAGGIMATE_SIM // DiagnosticLogPlugin uses WiFiUDP — device-only (PRO-266)
#include <display/plugins/DiagnosticLogPlugin.h>
#endif
#ifndef GAGGIMATE_HEADLESS
#ifdef GAGGIMATE_SIM
#include <SdlDriver.h> // desktop SDL panel stands in for the hardware drivers
#else
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#endif
#endif

const String LOG_TAG = F("Controller");

void Controller::setup() {
    // PRO-331: load persisted settings from NVS now, NOT in the Settings
    // constructor. Settings is a member of the global `controller`, so its
    // constructor runs during C++ static-init — before the Arduino core calls
    // nvs_flash_init(). Reading NVS that early fails on Arduino-esp32 3.x and
    // every value silently falls back to its default (WiFi never connects).
    // setup() runs after nvs init, so the read here succeeds.
    settings.load();

    mode = settings.getStartupMode();
    
    // Initialize process mutex for thread-safe access
    processMutex = xSemaphoreCreateMutex();
    if (processMutex == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to create process mutex");
    }

    // Web assets are served from this partition. LittleFS (not SPIFFS): SPIFFS
    // has no directory tree, so stat()/exists() is O(whole filesystem) and a
    // miss scans every page -- the web handler does that synchronously in the
    // async_tcp task for every request, which under a multi-tab load burst
    // pegged CPU0 for >5s and tripped the task watchdog (reboot). LittleFS
    // lookups are O(path). maxOpenFiles 16 for concurrent asset serving. [GM-90]
    if (!LittleFS.begin(true, "/littlefs", 16)) {
        Serial.println(F("An Error has occurred while mounting LittleFS"));
    }

#ifndef GAGGIMATE_HEADLESS
    setupPanel();
#endif

    pluginManager = new PluginManager();
#ifndef GAGGIMATE_HEADLESS
    ui = new DefaultUI(this, driver, pluginManager);
    if (driver->supportsSDCard() && driver->installSDCard()) {
        sdcard = true;
        ESP_LOGI(LOG_TAG, "SD Card detected and mounted");
        ESP_LOGI(LOG_TAG, "Used: %lluMB, Capacity: %lluMB", SD_MMC.usedBytes() / 1024 / 1024, SD_MMC.cardSize() / 1024 / 1024);
    }
#endif
    FS *fs = &LittleFS;
    if (sdcard) {
        fs = &SD_MMC;
    }
    beanManager = new BeanManager(fs, "/b");
    beanManager->setup();
    grinderManager = new GrinderManager(fs, "/g/grinders.json");
    grinderManager->setup();
    profileManager = new ProfileManager(fs, "/p", settings, pluginManager);
    profileManager->setup();
#if GAGGIMATE_ENABLE_HOMEKIT
    if (settings.isHomekit())
        pluginManager->registerPlugin(new HomekitPlugin(settings.getWifiSsid(), settings.getWifiPassword()));
    else
        pluginManager->registerPlugin(new mDNSPlugin());
#elif !defined(GAGGIMATE_SIM)
    // HomeKit compiled out: register mDNS unconditionally so the device stays
    // discoverable on the network (HomeKit otherwise provides its own mDNS).
    pluginManager->registerPlugin(new mDNSPlugin());
#endif
    if (settings.isBoilerFillActive()) {
        pluginManager->registerPlugin(new BoilerFillPlugin());
    }
    if (settings.isSmartGrindActive()) {
        pluginManager->registerPlugin(new SmartGrindPlugin());
    }
#if GAGGIMATE_ENABLE_MQTT
    if (settings.isHomeAssistant()) {
        pluginManager->registerPlugin(new MQTTPlugin());
    }
#endif
#if GAGGIMATE_ENABLE_WEBUI
    pluginManager->registerPlugin(new WebUIPlugin());
#endif
    pluginManager->registerPlugin(&ShotHistory);
#if GAGGIMATE_ENABLE_BLE_SCALE
    pluginManager->registerPlugin(&BLEScales);
#endif
    pluginManager->registerPlugin(new LedControlPlugin());
    pluginManager->registerPlugin(new AutoWakeupPlugin());
#ifndef GAGGIMATE_SIM
    // PRO-266: tees ESP_LOG over UDP for tether-free serial capture. Self-gated
    // on Settings::getDiagnosticLogEnabled() (default OFF) — registered
    // unconditionally so it can be toggled at runtime without a reflash.
    pluginManager->registerPlugin(new DiagnosticLogPlugin());
#endif
    pluginManager->setup(this);

    pluginManager->on("profiles:profile:save", [this](Event const &event) {
        String id = event.getString("id");
        if (id == profileManager->getSelectedProfile().id) {
            this->handleProfileUpdate();
        }
    });

    pluginManager->on("profiles:profile:select", [this](Event const &event) { this->handleProfileUpdate(); });

#ifndef GAGGIMATE_HEADLESS
    ui->init();
#endif
    this->onScreenReady();

    updateLastAction();
    xTaskCreatePinnedToCore(loopTask, "Controller::loopControl", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
}

void Controller::onScreenReady() { screenReady = true; }

void Controller::onTargetToggle() { settings.setVolumetricTarget(!settings.isVolumetricTarget()); }

void Controller::onTargetChange(ProcessTarget target) { settings.setVolumetricTarget(target == ProcessTarget::VOLUMETRIC); }

void Controller::connect() {
    if (initialized)
        return;
    lastPing = millis();
    connectStartTime = millis();
    pluginManager->trigger("controller:startup");

    setupWifi();
    setupBluetooth();
    pluginManager->on("ota:update:start", [this](Event const &) { this->updating = true; });
    pluginManager->on("ota:update:end", [this](Event const &) { this->updating = false; });

    updateLastAction();
    initialized = true;
}

#ifndef GAGGIMATE_HEADLESS
void Controller::setupPanel() {
#ifdef GAGGIMATE_SIM
    driver = SdlDriver::getInstance(); // desktop SDL panel
#else
    if (LilyGoDriver::getInstance()->isCompatible()) {
        driver = LilyGoDriver::getInstance();
    } else if (AmoledDisplayDriver::getInstance()->isCompatible()) {
        driver = AmoledDisplayDriver::getInstance();
    } else if (WaveshareDriver::getInstance()->isCompatible()) {
        driver = WaveshareDriver::getInstance();
    } else {
        Serial.println("No compatible display driver found");
        delay(10000);
        ESP.restart();
    }
#endif
    driver->init();
}
#endif

void Controller::setupBluetooth() {
#ifndef GAGGIMATE_SIM
    // PRO-330: connect() brings WiFi up (setupWifi()) immediately before this BLE
    // init. On the Arduino-esp32 3.x / IDF 5.x platform (PRO-293), the BT
    // controller's software-coexistence bring-up (coex_enable, reached from
    // esp_bt_controller_init/enable) ABORTS unless WiFi modem-sleep is enabled
    // (WIFI_PS_MIN_MODEM) — IDF logs "Should enable WiFi modem sleep when both
    // WiFi and Bluetooth are enabled" and the controller faults during its own
    // failed-init cleanup (LoadProhibited in btdm_controller_deinit_internal ->
    // uxListRemove). See NimBLE-Arduino#437 / espressif/esp-idf#9595.
    //
    // The Arduino WiFi.setSleep() path only applies esp_wifi_set_ps() on the
    // STA-start event, so the AP-fallback case (WiFi.softAP("GaggiMate"), the
    // common no-credentials path) never sets it and coexistence aborts. Force
    // the coexistence-required power-save mode directly here, after WiFi is up
    // and right before the BT controller initializes, covering STA and AP alike.
    // This was implicit on the old IDF 4.4 platform (where v156 ran); IDF 5.x
    // enforces it, which is why the NimBLE 1.x->2.x + platform bump regressed it.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#endif
    clientController.initClient();
    clientController.registerDisconnectCallback([this]() {
        if (initialized) {
            pluginManager->trigger("controller:bluetooth:disconnect");
            waitingForController = true;
            setMode(MODE_STANDBY);
        }
    });
    clientController.registerSensorCallback(
        [this](const float temp, const float pressure, const float puckFlow, const float pumpFlow, const float puckResistance) {
            onTempRead(temp);
            this->pressure = pressure;
            this->currentPuckFlow = puckFlow;
            this->currentPumpFlow = pumpFlow;
            pluginManager->trigger("boiler:pressure:change", "value", pressure);
            pluginManager->trigger("pump:puck-flow:change", "value", puckFlow);
            pluginManager->trigger("pump:flow:change", "value", pumpFlow);
            pluginManager->trigger("pump:puck-resistance:change", "value", puckResistance);
        });
    clientController.registerBrewBtnCallback([this](const int brewButtonStatus) { handleBrewButton(brewButtonStatus); });
    clientController.registerSteamBtnCallback([this](const int steamButtonStatus) { handleSteamButton(steamButtonStatus); });
    clientController.registerRemoteErrorCallback([this](const int error) {
        if (error != ERROR_CODE_TIMEOUT && error != this->error) {
            this->error = error;
            deactivate();
            setMode(MODE_STANDBY);
            pluginManager->trigger(F("controller:error"));
            ESP_LOGE(LOG_TAG, "Received error %d", error);
        }
    });
    clientController.registerAutotuneResultCallback([this](const float Kp, const float Ki, const float Kd, const float Kf) {
        ESP_LOGI(LOG_TAG, "Received autotune values: Kp=%.3f, Ki=%.3f, Kd=%.3f, Kf=%.3f (combined)", Kp, Ki, Kd, Kf);
        char pid[64];
        // Store in simplified format with combined Kf
        snprintf(pid, sizeof(pid), "%.3f,%.3f,%.3f,%.3f", Kp, Ki, Kd, Kf);
        settings.setPid(String(pid));
        pluginManager->trigger("controller:autotune:result");
        autotuning = false;
    });
    clientController.registerVolumetricMeasurementCallback(
        [this](const float value) { onVolumetricMeasurement(value, VolumetricMeasurementSource::FLOW_ESTIMATION); });
    clientController.registerTofMeasurementCallback([this](const int value) {
        tofDistance = value;
        ESP_LOGV(LOG_TAG, "Received new TOF distance: %d", value);
        pluginManager->trigger("controller:tof:change", "value", value);
    });
    pluginManager->trigger("controller:bluetooth:init");
}

void Controller::setupInfos() {
    const std::string info = clientController.readInfo();
#ifdef GAGGIMATE_SIM
    // PRO-243: the simulator's NimBLEClientController (sim/comms, intentionally
    // untouched) still serves the legacy JSON info string and the sim build does
    // not link the nanopb codegen, so keep parsing JSON here for the sim only.
    ESP_LOGI(LOG_TAG, "System info (sim/json): %s", info.c_str());
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, info);
    if (err) {
        ESP_LOGE(LOG_TAG, "Error deserializing JSON: %s", err.c_str());
        systemInfo = SystemInfo{
            .hardware = "GaggiMate Standard 1.x", .version = "v1.0.0", .capabilities = {.dimming = false, .pressure = false}};
    } else {
        systemInfo = SystemInfo{.hardware = doc["hw"].as<String>(),
                                .version = doc["v"].as<String>(),
                                .capabilities = SystemCapabilities{
                                    .dimming = doc["cp"]["dm"].as<bool>(),
                                    .pressure = doc["cp"]["ps"].as<bool>(),
                                    .ledControl = doc["cp"]["led"].as<bool>(),
                                    .tof = doc["cp"]["tof"].as<bool>(),
                                }};
    }
#else
    // PRO-243: nanopb SystemInfo wire format (was an ArduinoJson string). The
    // controller encodes gaggimate_SystemInfo in make_system_info(); decode it
    // back into the firmware's SystemInfo struct here.
    ESP_LOGI(LOG_TAG, "System info: %u bytes", static_cast<unsigned>(info.size()));
    gaggimate_SystemInfo msg = gaggimate_SystemInfo_init_zero;
    pb_istream_t is = pb_istream_from_buffer(reinterpret_cast<const uint8_t *>(info.data()), info.size());
    if (!pb_decode(&is, gaggimate_SystemInfo_fields, &msg)) {
        ESP_LOGE(LOG_TAG, "Error decoding SystemInfo: %s", PB_GET_ERROR(&is));
        systemInfo = SystemInfo{
            .hardware = "GaggiMate Standard 1.x", .version = "v1.0.0", .capabilities = {.dimming = false, .pressure = false}};
    } else {
        systemInfo = SystemInfo{.hardware = String(msg.hardware),
                                .version = String(msg.version),
                                .capabilities = SystemCapabilities{
                                    .dimming = msg.capabilities.dimming,
                                    .pressure = msg.capabilities.pressure,
                                    .ledControl = msg.capabilities.led_control,
                                    .tof = msg.capabilities.tof,
                                }};
    }
#endif
}

void Controller::setupWifi() {
    staConfigured = settings.getWifiSsid() != "" && settings.getWifiPassword() != "";
    if (staConfigured) {
        WiFi.setHostname(settings.getMdnsName().c_str());
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        // PRO-335 P2-2: register the WiFi event handlers UNCONDITIONALLY, before
        // WiFi.begin(), instead of inside the connect-success branch below. The old
        // placement meant that if the initial connect loop timed out (so we fell
        // through to SoftAP) but Arduino auto-reconnect later associated, the
        // STA_GOT_IP / STA_DISCONNECTED handlers were never installed, so
        // controller:wifi:connect / :disconnect never fired and the plugins
        // (mDNS / MQTT / DiagLog / WebUI relay) never (re)armed. Registering here
        // also backs the PRO-333 SoftAP-fallback watchdog, which relies on these
        // connect/disconnect transitions being observed for the whole STA lifetime.
        WiFi.onEvent(
            [this](WiFiEvent_t, WiFiEventInfo_t) {
                // PRO-333: a STA association/IP completing here re-arms the
                // watchdog. Without this, isApConnection is sticky: once a
                // SoftAP fallback set it true (router reboot, outage, out of
                // range, or the HomeKit ASSOC/AUTH loop) the wifiWatchdog()
                // guard (!staConnectedOnce || isApConnection) returned early
                // forever and the device stayed in AP mode until a manual
                // reboot — even after the home network came back. STA_GOT_IP
                // fires when the link is (re)established, so clear the AP flag,
                // mark that we have connected at least once, and reset the
                // down-clock. If we were AP-only, flip the radio back to STA so
                // the device returns to the LAN automatically.
                staConnectedOnce = true;
                staDownSince = 0;
                if (isApConnection) {
                    isApConnection = false;
                    WiFi.mode(WIFI_STA);
                }
                pluginManager->trigger("controller:wifi:connect", "AP", 0);
            },
            WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(
            [this](WiFiEvent_t, WiFiEventInfo_t info) {
                ESP_LOGI(LOG_TAG, "Lost WiFi connection. Reason: %s",
                         WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
                pluginManager->trigger("controller:wifi:disconnect");
            },
            WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        WiFi.begin(settings.getWifiSsid(), settings.getWifiPassword());
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        for (int attempts = 0; attempts < WIFI_CONNECT_ATTEMPTS; attempts++) {
            if (WiFi.status() == WL_CONNECTED) {
                break;
            }
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(LOG_TAG, "Connected to %s with IP address %s", settings.getWifiSsid().c_str(),
                     WiFi.localIP().toString().c_str());
            staConnectedOnce = true;
            staDownSince = 0;
            // PRO-335 P1-1: configTzTime() already starts SNTP (via
            // esp_netif_sntp_init). The previous manual sntp_set_sync_mode() +
            // sntp_setservername() + sntp_init() trio re-initialized an
            // already-initialized SNTP service, which asserts/abort()s on IDF 5.x
            // ("SNTP already initialized") on every successful WiFi connect.
            //
            // SMOOTH sync (slew the clock instead of stepping it) was the
            // intended behavior, so keep it — but set it BEFORE configTzTime().
            // esp_netif_sntp_init() honors a pre-set sync mode, so setting it
            // first avoids the post-init re-init abort while preserving SMOOTH.
            sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
            configTzTime(resolve_timezone(settings.getTimezone()), NTP_SERVER);
            setenv("TZ", resolve_timezone(settings.getTimezone()), 1);
            tzset();
        } else {
            WiFi.disconnect(true, true);
            ESP_LOGI(LOG_TAG, "Timed out while connecting to WiFi");
            Serial.println("Timed out while connecting to WiFi");
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        startSoftAp();
    }

    pluginManager->on("ota:update:start", [this](Event const &) { this->updating = true; });
    pluginManager->on("ota:update:end", [this](Event const &) { this->updating = false; });

    pluginManager->trigger("controller:wifi:connect", "AP", isApConnection ? 1 : 0);
}

// PRO-333: factored out of setupWifi() so wifiWatchdog() can re-open the SoftAP
// when a previously-good STA connection is lost.
//
// When STA credentials are configured, fall back to WIFI_AP_STA (NOT AP-only):
// keeping the STA interface alive lets Arduino auto-reconnect keep retrying the
// home network in the background while the SoftAP serves the web UI. That is
// what makes the router-reboot / out-of-range case auto-recover — when the home
// network returns, the STA re-associates and the ARDUINO_EVENT_WIFI_STA_GOT_IP
// handler (above) clears isApConnection and flips the radio back to WIFI_STA. In
// AP-only mode the STA radio is off, STA_GOT_IP can never fire, and the device
// would stay on its SoftAP forever (the sticky-isApConnection bug). With no
// credentials there is no STA to keep alive, so use plain WIFI_AP.
void Controller::startSoftAp() {
    isApConnection = true;
    staDownSince = 0; // AP is the reachable surface now; stop the STA-down clock.
    WiFi.mode(staConfigured ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, WIFI_SUBNET_MASK);
    WiFi.softAP(WIFI_AP_SSID);
    if (staConfigured) {
        // Keep the STA trying the home network in the background so a later
        // recovery fires STA_GOT_IP and pulls us back onto the LAN. begin()
        // here is harmless if a connect is already in flight.
        WiFi.begin(settings.getWifiSsid(), settings.getWifiPassword());
    }
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    ESP_LOGI(LOG_TAG, "Started WiFi AP %s", WIFI_AP_SSID);
}

// PRO-333: WiFi STA recovery / SoftAP-fallback watchdog.
//
// setupWifi() only falls back to SoftAP if the *initial* connect fails. Once a
// STA connection succeeds it is never re-checked, so a later sustained STA loss
// (the HomeKit/HomeSpan ASSOC_LEAVE -> AUTH_EXPIRE loop, a router reboot, going
// out of range) leaves the device stranded: off the LAN and never on its own
// SoftAP. This watchdog runs each loop tick and, for a STA-configured device
// that is NOT already in AP mode, tracks how long the link has been down and:
//   - within the grace window: does nothing (Arduino auto-reconnect heals blips)
//   - past grace: re-asserts STA ownership with an explicit reconnect
//   - past the fallback window: opens SoftAP so the user is never locked out
//
// The pure decision lives in WiFiFallbackPolicy.h (host-tested); this method
// only owns the timing bookkeeping and the WiFi side effects.
void Controller::wifiWatchdog() {
    // Only babysit a configured STA, and only once it has actually connected at
    // least once (a never-connected STA is handled by setupWifi's AP fallback).
    if (!staConfigured || !staConnectedOnce || isApConnection) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastWifiWatchdog < WIFI_WATCHDOG_INTERVAL_MS) {
        return;
    }
    lastWifiWatchdog = now;

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        staDownSince = 0;
        return;
    }

    // Link is down. Start (or continue) the down-clock.
    if (staDownSince == 0) {
        staDownSince = now;
    }
    const unsigned long downForMs = now - staDownSince;

    // `connected` is provably false here: the early return above bails when
    // WiFi.status() == WL_CONNECTED, so the watchdog only reaches this point on
    // a down link. Pass a literal false rather than `connected` so the call site
    // does not imply a live runtime branch that cannot occur. The pure function
    // keeps the parameter for host-test coverage of both values.
    switch (wifiWatchdogAction(/*staConnected*/ false, isApConnection, staConfigured, downForMs, WIFI_WATCHDOG_GRACE_MS,
                               WIFI_WATCHDOG_FALLBACK_MS)) {
    case WiFiWatchdogAction::RECONNECT:
        ESP_LOGW(LOG_TAG, "WiFi STA down %lums, re-asserting connection to %s", downForMs, settings.getWifiSsid().c_str());
        // Re-own the radio with an explicit reconnect. This matters when HomeKit
        // is enabled: HomeSpan's init() calls WiFi.setAutoReconnect(false) (it
        // expects to manage reconnects itself), and the PRO-333 fix neutralizes
        // HomeSpan's WiFi.begin() via setWifiBegin(), so nothing else re-associates
        // a dropped STA. reconnect() re-issues the association with our configured
        // credentials intact.
        WiFi.reconnect();
        break;
    case WiFiWatchdogAction::OPEN_SOFTAP:
        ESP_LOGW(LOG_TAG, "WiFi STA down %lums, falling back to SoftAP so the web UI stays reachable", downForMs);
        startSoftAp();
        {
            // PRO-333: this runs on the Arduino main loop task (Controller::loop()
            // -> wifiWatchdog()), unlike the other controller:wifi:connect triggers
            // (setupWifi on the setup task, the STA_GOT_IP handler on the
            // arduino_events WiFi-event task). WebUIPlugin::start()/stop() carry
            // AsyncTCP task affinity and a documented "never run synchronously from
            // the watchdog" invariant, so tag this trigger `deferred=1`: WebUIPlugin
            // latches it and runs start() from its own loop()-task deferred-intent
            // drain instead of inline on this synchronous trigger path.
            Event apRearm;
            apRearm.id = "controller:wifi:connect";
            apRearm.setInt("AP", 1);
            apRearm.setInt("deferred", 1);
            pluginManager->trigger(apRearm);
        }
        break;
    case WiFiWatchdogAction::NONE:
        break;
    }
}

void Controller::loop() {
    pluginManager->loop();

    if (screenReady) {
        connect();
    }

    // PRO-333: babysit the STA link so a sustained loss re-asserts STA and
    // ultimately falls back to SoftAP instead of leaving the user locked out.
    if (initialized) {
        wifiWatchdog();
    }

    unsigned long now = millis();

    // If BLE scanning has been running for a while without finding the controller,
    // notify the UI so it can update the startup label accordingly.
    if (!waitingForController && initialized && !clientController.isConnected() &&
        (long)(now - connectStartTime) > CONTROLLER_WAITING_TIMEOUT_MS) {
        waitingForController = true;
        pluginManager->trigger("controller:bluetooth:waiting");
    }

    if (clientController.isReadyForConnection() && clientController.connectToServer()) {
        waitingForController = false;
        setupInfos();
        ESP_LOGI(LOG_TAG, "setting pressure scale to %.2f", settings.getPressureScaling());
        setPressureScale();
        clientController.sendPidSettings(settings.getPid());
        clientController.sendPumpModelCoeffs(settings.getPumpModelCoeffs());
        if (!loaded) {
            loaded = true;
            if (settings.getStartupMode() == MODE_STANDBY)
                activateStandby();

            pluginManager->trigger("controller:ready");
        }
        pluginManager->trigger("controller:bluetooth:connect");
    }

    if (isErrorState()) {
        return;
    }

    if (now - lastProgress > PROGRESS_INTERVAL) {
        // Check if steam is ready. Steam should only become ready at the
        // actual target temperature, and should require reheating after a drop.
        if (mode == MODE_STEAM) {
            const float targetTemp = getTargetTemp();
            if (targetTemp > 0.0f && currentTemp < targetTemp) {
                steamReady = false;
            } else if (!steamReady && targetTemp > 0.0f && currentTemp >= targetTemp) {
                activate();
                steamReady = true;
            }
        }

        // Handle current process with mutex protection
        if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (currentProcess != nullptr) {
                updateLastAction();
                if (currentProcess->getType() == MODE_BREW) {
                    auto brewProcess = static_cast<BrewProcess *>(currentProcess);
                    brewProcess->updatePressure(pressure);
                    brewProcess->updateFlow(currentPumpFlow);
                    brewProcess->setVolumetricAvailable(isActiveVolumetricSourceLive());
                }
                currentProcess->progress();
                bool stillActive = currentProcess->isActive();
                xSemaphoreGive(processMutex);
                
                if (!stillActive) {
                    deactivate();
                }
            } else {
                xSemaphoreGive(processMutex);
            }
        }

        // Handle last process - Calculate auto delay (mutex protected against clear()/deactivate())
        if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (lastProcess != nullptr && !lastProcess->isComplete()) {
                lastProcess->progress();
            }
            if (lastProcess != nullptr && lastProcess->isComplete() && !processCompleted && settings.isDelayAdjust()) {
                processCompleted = true;
                if (lastProcess->getType() == MODE_BREW) {
                    if (auto *brewProcess = static_cast<BrewProcess *>(lastProcess);
                        brewProcess->target == ProcessTarget::VOLUMETRIC) {
                        double newDelay = brewProcess->getNewDelayTime();
                        if (newDelay >= 0) {
                            settings.setBrewDelay(newDelay);
                        }
                    }
                } else if (lastProcess->getType() == MODE_GRIND) {
                    if (auto *grindProcess = static_cast<GrindProcess *>(lastProcess);
                        grindProcess->target == ProcessTarget::VOLUMETRIC) {
                        double newDelay = grindProcess->getNewDelayTime();
                        if (newDelay >= 0) {
                            settings.setGrindDelay(newDelay);
                        }
                    }
                }
            }
            lastProgress = now;
            xSemaphoreGive(processMutex);
        }
    }

    if (grindActiveUntil != 0 && (long)(now - grindActiveUntil) > 0)
        deactivateGrind();
    if (mode != MODE_STANDBY && settings.getStandbyTimeout() > 0 && (long)(now - lastAction) > settings.getStandbyTimeout())
        activateStandby();
}

void Controller::loopControl() {
    if (initialized) {
        updateControl();
    }
}

bool Controller::isUpdating() const { return updating; }

bool Controller::isAutotuning() const { return autotuning; }

bool Controller::isReady() const { return !isUpdating() && !isErrorState() && !isAutotuning(); }

bool Controller::isVolumetricAvailable() const {
#ifdef NIGHTLY_BUILD
    return isBluetoothScaleHealthy() || systemInfo.capabilities.dimming;
#else
    return isBluetoothScaleHealthy();
#endif
}

// True when the volumetric source that the *active* shot is actually consuming
// is still delivering usable measurements. This is the correct signal for the
// CAR-367 duration-cap suppression gate, NOT isVolumetricAvailable():
// onVolumetricMeasurement() rejects any measurement whose source != the latched
// currentVolumetricSource (Controller.cpp), so once a shot starts on BLUETOOTH
// it only consumes BLE weights for its lifetime. Under NIGHTLY_BUILD on a
// dimming-capable controller, isVolumetricAvailable() returns true even with a
// dead scale (flow-estimation capability), but the BLE-sourced shot's currentVolume
// would go stale — leaving suppression on and running to BREW_SAFETY_DURATION_MS.
// Gate on the health of the active source instead: BLUETOOTH needs a healthy
// scale; FLOW_ESTIMATION is always live; INACTIVE is not volumetric.
// Depends on the cross-shot invariant that currentVolumetricSource is reset to
// INACTIVE in clear() between shots, so a stale BLUETOOTH source can't leak in.
bool Controller::isActiveVolumetricSourceLive() const {
    switch (currentVolumetricSource) {
    case VolumetricMeasurementSource::BLUETOOTH:
        return isBluetoothScaleHealthy();
    case VolumetricMeasurementSource::FLOW_ESTIMATION:
        return true;
    case VolumetricMeasurementSource::INACTIVE:
    default:
        return false;
    }
}

void Controller::autotune(int testTime, int samples) {
    if (isActiveSafe() || !isReady()) {
        return;
    }
    if (mode != MODE_STANDBY) {
        activateStandby();
    }
    autotuning = true;
    clientController.sendAutotune(testTime, samples);
    pluginManager->trigger("controller:autotune:start");
}

void Controller::startProcess(Process *process) {
    if (!isReady()) {
        delete process;
        return;
    }
    
    // Acquire mutex first to prevent TOCTOU race condition
    // Use portMAX_DELAY (blocking) with ESP_LOGE: failure here is critical and should never happen
    if (xSemaphoreTake(processMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(LOG_TAG, "Failed to acquire mutex in startProcess");
        delete process;
        return;
    }
    
    // Check if process is already active while holding the mutex
    if (currentProcess != nullptr && currentProcess->isActive()) {
        xSemaphoreGive(processMutex);
        delete process;
        return;
    }

    if (!process->isActive()) {
        const int endedProcessType = process->getType();
        xSemaphoreGive(processMutex);
        delete process;
        if (endedProcessType == MODE_BREW) {
            pluginManager->trigger("controller:brew:end");
        } else if (endedProcessType == MODE_GRIND) {
            pluginManager->trigger("controller:grind:end");
        }
        pluginManager->trigger("controller:process:end", "processType", endedProcessType);
        updateLastAction();
        return;
    }
    
    processCompleted = false;
    this->currentProcess = process;
    
    xSemaphoreGive(processMutex);
    
    pluginManager->trigger("controller:process:start");
    updateLastAction();
}

float Controller::getTargetTemp() const {
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        // If we can't get mutex, return safe default based on mode
        switch (mode) {
        case MODE_STANDBY:
            return profileManager->getSelectedProfile().temperature;
        case MODE_BREW:
            return profileManager->getSelectedProfile().temperature;
        case MODE_STEAM:
            return settings.getTargetSteamTemp();
        case MODE_WATER:
            return settings.getTargetWaterTemp();
        case MODE_MANUAL:
            return settings.getManualTemperature();
        default:
            return 0;
        }
    }
    
    Process *proc = currentProcess;
    float result = 0;
    
    switch (mode) {
    case MODE_STANDBY:
        result = profileManager->getSelectedProfile().temperature;
        break;
    case MODE_BREW:
        if (proc != nullptr && proc->isActive() && proc->getType() == MODE_BREW) {
            auto brewProcess = static_cast<BrewProcess *>(proc);
            result = brewProcess->getTemperature();
        } else {
            result = profileManager->getSelectedProfile().temperature;
        }
        break;
    case MODE_STEAM:
        result = settings.getTargetSteamTemp();
        break;
    case MODE_WATER:
        result = settings.getTargetWaterTemp();
        break;
    case MODE_MANUAL:
        result = settings.getManualTemperature();
        break;
    default:
        result = 0;
        break;
    }
    
    xSemaphoreGive(processMutex);
    return result;
}

// Computes the first phase's configured pressure value for standby preview.
// Returns true and sets `out` when the selected profile's first phase uses an
// advanced pump with an applicable (non-sentinel) value; returns false when no
// target is applicable (simple pump, empty profile, or the "hold current value"
// -1 sentinel that can't be resolved without a live measurement). For an
// advanced phase the active-brew path publishes BOTH pressure and flow (the
// non-primary axis is the configured limit), so standby previews both —
// keeping the readout consistent with the first brew tick.
static bool firstPhasePressureTarget(const Profile &profile, float &out) {
    if (profile.phases.empty()) {
        return false;
    }
    const Phase &first = profile.phases[0];
    if (first.pumpIsSimple) {
        return false;
    }
    // -1 is the "hold current value at phase start" sentinel, resolved to a live
    // measurement by BrewProcess during a brew. In standby there is no measurement
    // to resolve against, so report it as unavailable rather than a misleading 0.
    if (first.pumpAdvanced.pressure < 0.0f) {
        return false;
    }
    out = first.pumpAdvanced.pressure;
    return true;
}

// Computes the first phase's configured flow value for standby preview.
// See firstPhasePressureTarget for semantics.
static bool firstPhaseFlowTarget(const Profile &profile, float &out) {
    if (profile.phases.empty()) {
        return false;
    }
    const Phase &first = profile.phases[0];
    if (first.pumpIsSimple) {
        return false;
    }
    if (first.pumpAdvanced.flow < 0.0f) {
        return false;
    }
    out = first.pumpAdvanced.flow;
    return true;
}

float Controller::getTargetPressure() const {
    if (mode == MODE_STANDBY) {
        float v = 0.0f;
        firstPhasePressureTarget(profileManager->getSelectedProfile(), v);
        return v;
    }
    return targetPressure;
}

float Controller::getTargetFlow() const {
    if (mode == MODE_STANDBY) {
        float v = 0.0f;
        firstPhaseFlowTarget(profileManager->getSelectedProfile(), v);
        return v;
    }
    return targetFlow;
}

// Reports whether a pump pressure/flow target is actually applicable for the
// current active process — used so WebUIPlugin can emit JSON null (not a
// misleading 0) when there is no target. Mirrors updateControl()'s branch
// logic: the entire target-setting block is gated on
// systemInfo.capabilities.pressure (Controller.cpp ~L856), so without pressure
// support NO mode drives a pump target. With pressure support, only
// advanced-pump brews, manual, and steam set real targets; simple-pump brews,
// water, grind, and the inactive fallthrough leave the members at 0. (Standby
// is handled by the callers via the first-phase helpers, so it never reaches
// here.)
//
// Takes processMutex before touching currentProcess: status serialization runs
// on a different task than activate()/deactivate(), which can move and delete
// the process — dereferencing it unlocked would be a use-after-free. On mutex
// timeout we return false (treat the target as unavailable) rather than risk it.
bool Controller::hasPumpTarget() const {
    // No pressure hardware → updateControl() never populates pump targets in any
    // mode, so they are always an unavailable 0.
    if (!systemInfo.capabilities.pressure) {
        return false;
    }
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    bool result = false;
    Process *proc = currentProcess;
    if (proc != nullptr && proc->isActive()) {
        switch (proc->getType()) {
        case MODE_BREW:
            result = static_cast<BrewProcess *>(proc)->isAdvancedPump();
            break;
        case MODE_MANUAL:
        case MODE_STEAM:
            result = true;
            break;
        default: // MODE_WATER, MODE_GRIND — no pump pressure/flow target
            result = false;
            break;
        }
    }
    xSemaphoreGive(processMutex);
    return result;
}

bool Controller::hasTargetPressure() const {
    if (mode == MODE_STANDBY) {
        float v = 0.0f;
        return firstPhasePressureTarget(profileManager->getSelectedProfile(), v);
    }
    return hasPumpTarget();
}

bool Controller::hasTargetFlow() const {
    if (mode == MODE_STANDBY) {
        float v = 0.0f;
        return firstPhaseFlowTarget(profileManager->getSelectedProfile(), v);
    }
    return hasPumpTarget();
}

void Controller::setTargetTemp(float temperature) {
    pluginManager->trigger("boiler:targetTemperature:change", "value", temperature);
    switch (mode) {
    case MODE_BREW:
        profileManager->getSelectedProfile().temperature = temperature;
        break;
    case MODE_STEAM:
        settings.setTargetSteamTemp(static_cast<int>(temperature));
        break;
    case MODE_WATER:
        settings.setTargetWaterTemp(static_cast<int>(temperature));
        break;
    case MODE_MANUAL:
        settings.setManualTemperature(static_cast<int>(temperature));
        break;
    default:;
    }
    updateLastAction();
}

void Controller::setPressureScale(void) {
    if (systemInfo.capabilities.pressure) {
        clientController.setPressureScale(settings.getPressureScaling());
    }
}

void Controller::setPumpModelCoeffs(void) {
    if (systemInfo.capabilities.dimming) {
        clientController.sendPumpModelCoeffs(settings.getPumpModelCoeffs());
    }
}

int Controller::getTargetGrindDuration() const { return settings.getTargetGrindDuration(); }

void Controller::setTargetGrindDuration(int duration) {
    Event event = pluginManager->trigger("controller:grindDuration:change", "value", duration);
    settings.setTargetGrindDuration(event.getInt("value"));
    updateLastAction();
}

void Controller::setTargetGrindVolume(double volume) {
    Event event = pluginManager->trigger("controller:grindVolume:change", "value", static_cast<float>(volume));
    settings.setTargetGrindVolume(event.getFloat("value"));
    updateLastAction();
}

void Controller::updateManualTargets(int targetType, float pressure, float flow, int temperature) {
    settings.setManualTargetType(targetType);
    settings.setManualPressure(pressure);
    settings.setManualFlow(flow);
    settings.setManualTemperature(temperature);
    setTargetTemp(settings.getManualTemperature());

    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in updateManualTargets");
        return;
    }

    if (currentProcess != nullptr && currentProcess->getType() == MODE_MANUAL) {
        auto *manual = static_cast<ManualProcess *>(currentProcess);
        manual->updateTargets(settings.getManualTargetType(), settings.getManualPressure(), settings.getManualFlow(),
                              settings.getManualTemperature());
    }

    xSemaphoreGive(processMutex);
    updateControl();
    updateLastAction();
}

void Controller::raiseTemp() {
    float temp = getTargetTemp();
    temp = constrain(temp + 1.0f, MIN_TEMP, MAX_TEMP);
    setTargetTemp(temp);
}

void Controller::lowerTemp() {
    float temp = getTargetTemp();
    temp = constrain(temp - 1.0f, MIN_TEMP, MAX_TEMP);
    setTargetTemp(temp);
}

void Controller::raiseBrewTarget() {
    if (isVolumetricAvailable() && profileManager->getSelectedProfile().isVolumetric()) {
        // CAR-375: for volumetric profiles these buttons edit per-shot YIELD,
        // which is locked to the profile when the override is disabled. The
        // duration path below is not yield, so it stays reachable.
        if (!settings.isAllowYieldOverride()) {
            return;
        }
        profileManager->getSelectedProfile().adjustVolumetricTarget(1);
    } else {
        profileManager->getSelectedProfile().adjustDuration(1);
    }
    handleProfileUpdate();
}

void Controller::lowerBrewTarget() {
    if (isVolumetricAvailable() && profileManager->getSelectedProfile().isVolumetric()) {
        // CAR-375: see raiseBrewTarget — only the volumetric YIELD path honors
        // the yield lock; duration adjustment remains available.
        if (!settings.isAllowYieldOverride()) {
            return;
        }
        profileManager->getSelectedProfile().adjustVolumetricTarget(-1);
    } else {
        profileManager->getSelectedProfile().adjustDuration(-1);
    }
    handleProfileUpdate();
}

void Controller::setBrewTarget(float value) {
    // Apply absolute brew target from the dashboard YIELD slider. When the
    // active profile is volumetric, set the cumulative target across brew
    // phases. Otherwise the dashboard YIELD has no meaning — ignore so we
    // don't accidentally rewrite a time-based profile's duration. Mirrors
    // the in-memory mutation pattern of raise/lowerBrewTarget — the change
    // applies to the next shot but is NOT persisted to disk; reloading the
    // profile restores the saved target.
    //
    // CAR-375: when the per-shot yield override is disabled, the yield is
    // locked to the profile. Ignore any incoming brew-target so the display
    // and any stale web client honor the lock.
    if (!settings.isAllowYieldOverride()) {
        return;
    }
    if (!profileManager->getSelectedProfile().isVolumetric()) {
        return;
    }
    profileManager->getSelectedProfile().setVolumetricTarget(value);
    handleProfileUpdate();
}

void Controller::raiseGrindTarget() {
    if (settings.isVolumetricTarget() && isVolumetricAvailable()) {
        double newTarget = settings.getTargetGrindVolume() + 0.5;
        if (newTarget > BREW_MAX_VOLUMETRIC) {
            newTarget = BREW_MAX_VOLUMETRIC;
        }
        setTargetGrindVolume(newTarget);
    } else {
        int newDuration = getTargetGrindDuration() + 1000;
        if (newDuration > BREW_MAX_DURATION_MS) {
            newDuration = BREW_MAX_DURATION_MS;
        }
        setTargetGrindDuration(newDuration);
    }
}

void Controller::lowerGrindTarget() {
    if (settings.isVolumetricTarget() && isVolumetricAvailable()) {
        double newTarget = settings.getTargetGrindVolume() - 0.5;
        if (newTarget < BREW_MIN_VOLUMETRIC) {
            newTarget = BREW_MIN_VOLUMETRIC;
        }
        setTargetGrindVolume(newTarget);
    } else {
        int newDuration = getTargetGrindDuration() - 1000;
        if (newDuration < BREW_MIN_DURATION_MS) {
            newDuration = BREW_MIN_DURATION_MS;
        }
        setTargetGrindDuration(newDuration);
    }
}

void Controller::updateControl() {
    // Thread-safe access to currentProcess with mutex protection
    // Hold mutex for entire duration to prevent use-after-free
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Skip this update if we can't get the mutex quickly
    }
    
    Process *proc = currentProcess;
    bool active = proc != nullptr && proc->isActive();
    
    // Copy values we need while holding the mutex to minimize lock time
    bool isAltRelayActive = false;
    int procType = -1;
    float pumpValue = 0.0f;
    bool relayActive = false;
    bool isAdvancedPump = false;
    bool brewPumpTargetIsPressure = false;
    float brewPumpPressure = 0.0f;
    float brewPumpFlow = 0.0f;
    bool manualTargetIsPressure = true;
    float manualPumpPressure = 0.0f;
    float manualPumpFlow = 0.0f;
    float targetTemp = 0.0f;
    
    if (active) {
        procType = proc->getType();
        pumpValue = proc->getPumpValue();
        relayActive = proc->isRelayActive();
        isAltRelayActive = proc->isAltRelayActive();
        
        if (procType == MODE_BREW) {
            auto *brewProcess = static_cast<BrewProcess *>(proc);
            isAdvancedPump = brewProcess->isAdvancedPump();
            if (isAdvancedPump) {
                brewPumpTargetIsPressure = (brewProcess->getPumpTarget() == PumpTarget::PUMP_TARGET_PRESSURE);
                brewPumpPressure = brewProcess->getPumpPressure();
                brewPumpFlow = brewProcess->getPumpFlow();
            }
            targetTemp = brewProcess->getTemperature();
        } else if (procType == MODE_MANUAL) {
            auto *manualProcess = static_cast<ManualProcess *>(proc);
            manualTargetIsPressure = manualProcess->isPressureTarget();
            manualPumpPressure = manualProcess->getPumpPressure();
            manualPumpFlow = manualProcess->getPumpFlow();
            targetTemp = manualProcess->getTemperature();
        }
    }
    
    // Get target temp while still holding mutex to avoid race condition
    // Inline the logic from getTargetTemp() to avoid deadlock
    if (targetTemp == 0.0f) {
        switch (mode) {
        case MODE_BREW:
            targetTemp = profileManager->getSelectedProfile().temperature;
            break;
        case MODE_STEAM:
            targetTemp = settings.getTargetSteamTemp();
            break;
        case MODE_WATER:
            targetTemp = settings.getTargetWaterTemp();
            break;
        case MODE_MANUAL:
            targetTemp = settings.getManualTemperature();
            break;
        default:
            targetTemp = 0;
            break;
        }
    }
    
    // Release mutex now that we've copied all needed values
    xSemaphoreGive(processMutex);

    if (targetTemp > .0f) {
        targetTemp = targetTemp + static_cast<float>(settings.getTemperatureOffset());
    }

    bool altRelayActive = false;
    if (active && isAltRelayActive) {
        if (procType == MODE_GRIND && settings.getAltRelayFunction() == ALT_RELAY_GRIND) {
            altRelayActive = true;
        }
    }

    clientController.sendAltControl(altRelayActive);
    if (active && systemInfo.capabilities.pressure) {
        if (procType == MODE_STEAM) {
            targetPressure = settings.getSteamPumpCutoff();
            targetFlow = pumpValue * 0.1f;
            clientController.sendAdvancedOutputControl(false, targetTemp, false, targetPressure, targetFlow);
            return;
        }
        if (procType == MODE_BREW) {
            if (isAdvancedPump) {
                clientController.sendAdvancedOutputControl(relayActive, targetTemp,
                                                           brewPumpTargetIsPressure,
                                                           brewPumpPressure, brewPumpFlow);
                targetPressure = brewPumpPressure;
                targetFlow = brewPumpFlow;
                return;
            }
        }
        if (procType == MODE_MANUAL) {
            targetPressure = manualPumpPressure;
            targetFlow = manualPumpFlow;
            clientController.sendAdvancedOutputControl(relayActive, targetTemp, manualTargetIsPressure, manualPumpPressure,
                                                       manualPumpFlow);
            return;
        }
    }
    targetPressure = 0.0f;
    targetFlow = 0.0f;
    clientController.sendOutputControl(active && relayActive, active ? pumpValue : 0, targetTemp);
}

void Controller::activate() {
    if (isActiveSafe())
        return;
    // Any activate() call while in standby (web/remote req:process:activate,
    // or the LVGL onBrewStart path) mirrors the physical brew button's first
    // press: wake into BREW (boiler starts heating). A second activate() then
    // starts the shot. See handleBrewButton() MODE_STANDBY case.
    //
    // Not covered by a native unit test: the host test env (build_src_filter)
    // compiles only test/native/* + PluginManager.cpp, and a real Controller
    // pulls in SD_MMC/SPIFFS/BLE/LVGL/FreeRTOS + ~10 plugins, which the harness
    // does not shim. This guard is exercised by the firmware compile in CI and
    // is symmetric with the handleBrewButton() standby case above.
    if (mode == MODE_STANDBY) {
        deactivateStandby();
        return;
    }
    clear();
    // Tare + settle is only meaningful for modes that consume scale/volumetric
    // data. Steam and water modes have no scale and no volumetric phase, so
    // running them here would block the controller task on a synchronous BLE
    // write-with-response and a 200 ms settle for nothing — visibly freezing
    // LVGL on the shared core (see CAR-253).
    const bool needsTare = (mode == MODE_BREW || mode == MODE_MANUAL);
    if (needsTare) {
        clientController.tare();
        if (isVolumetricAvailable()) {
#ifdef NIGHTLY_BUILD
            currentVolumetricSource =
                isBluetoothScaleHealthy() ? VolumetricMeasurementSource::BLUETOOTH : VolumetricMeasurementSource::FLOW_ESTIMATION;
#else
            currentVolumetricSource = VolumetricMeasurementSource::BLUETOOTH;
#endif
            if (mode == MODE_BREW) {
                pluginManager->trigger("controller:brew:prestart");
            }
        }
        // Yield to FreeRTOS (including WiFi/BLE tasks) while waiting for the scale
        // tare to settle.  Arduino delay() spins without yielding and can starve
        // the WiFi TCP/IP stack, causing WebSocket disconnections.
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    switch (mode) {
    case MODE_BREW:
        startProcess(new BrewProcess(profileManager->getSelectedProfile(),
                                     profileManager->getSelectedProfile().isVolumetric() && isVolumetricAvailable()
                                         ? ProcessTarget::VOLUMETRIC
                                         : ProcessTarget::TIME,
                                     settings.getBrewDelay()));
        break;
    case MODE_STEAM:
        startProcess(new SteamProcess(STEAM_SAFETY_DURATION_MS, settings.getSteamPumpPercentage()));
        break;
    case MODE_WATER:
        startProcess(new PumpProcess());
        break;
    case MODE_MANUAL:
        if (!isManualAvailable())
            return;
        startProcess(new ManualProcess(settings.getManualTargetType(), settings.getManualPressure(), settings.getManualFlow(),
                                       settings.getManualTemperature()));
        break;
    default:;
    }
    
    // Check if we started a brew process (with mutex protection)
    bool isBrewProcess = false;
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        isBrewProcess = currentProcess != nullptr && currentProcess->getType() == MODE_BREW;
        xSemaphoreGive(processMutex);
    }
    
    if (isBrewProcess) {
        pluginManager->trigger("controller:brew:start");
    }
}

void Controller::deactivate() {
    // Use portMAX_DELAY (blocking) with ESP_LOGE: failure here is critical and should never happen
    if (xSemaphoreTake(processMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(LOG_TAG, "Failed to acquire mutex in deactivate");
        return;
    }
    
    if (currentProcess == nullptr) {
        xSemaphoreGive(processMutex);
        return;
    }
    delete lastProcess;
    lastProcess = currentProcess;
    currentProcess = nullptr;
    const int endedProcessType = lastProcess->getType();
    
    xSemaphoreGive(processMutex);
    if (endedProcessType == MODE_BREW) {
        pluginManager->trigger("controller:brew:end");
    } else if (endedProcessType == MODE_GRIND) {
        pluginManager->trigger("controller:grind:end");
    }
    pluginManager->trigger("controller:process:end", "processType", endedProcessType);
    updateLastAction();
}

void Controller::clear() {
    processCompleted = true;
    
    // Protect lastProcess access with mutex to prevent race with getProcessSnapshot() and onVolumetricMeasurement()
    if (xSemaphoreTake(processMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(LOG_TAG, "Failed to acquire mutex in clear");
        return;
    }
    
    if (lastProcess != nullptr && lastProcess->getType() == MODE_BREW) {
        pluginManager->trigger("controller:brew:clear");
    }
    delete lastProcess;
    lastProcess = nullptr;
    
    xSemaphoreGive(processMutex);
    
    currentVolumetricSource = VolumetricMeasurementSource::INACTIVE;
}

void Controller::activateGrind() {
    if (!isGrindAvailable())
        return;
    pluginManager->trigger("controller:grind:start");
    if (isGrindActive())
        return;
    clear();
    if (settings.isVolumetricTarget() && isVolumetricAvailable()) {
        currentVolumetricSource = VolumetricMeasurementSource::BLUETOOTH;
        startProcess(new GrindProcess(ProcessTarget::VOLUMETRIC, 0, settings.getTargetGrindVolume(), settings.getGrindDelay()));
    } else {
        startProcess(
            new GrindProcess(ProcessTarget::TIME, settings.getTargetGrindDuration(), settings.getTargetGrindVolume(), 0.0));
    }
}

void Controller::deactivateGrind() {
    deactivate();
    clear();
}

void Controller::activateStandby() {
    // PRO-278: tear the running process down BEFORE flipping the mode, never
    // the other way around. The reverse order (setMode then deactivate) leaves
    // a window in which mode == MODE_STANDBY while the steam/brew process is
    // still currentProcess and isActive(). In that window setMode() has already
    // dispatched the mutable `controller:mode:change` event, and a re-assert
    // path (a still-active SteamProcess, the steam UI screen, or a mode-change
    // handler) can flip the mode back to MODE_STEAM/MODE_BREW before deactivate()
    // lands — the user-reported "stop-steam bounces back, second press sticks"
    // bug. Deactivating first means the mode-change event fires with no active
    // process to re-assert against, so a single press lands in Standby and stays.
    // This matches every sibling teardown: deactivateStandby(), the steam-button
    // release in handleSteamButton(), and WebUIPlugin's req:change-mode STANDBY
    // path all deactivate() before setMode().
    deactivate();
    setMode(MODE_STANDBY);
}

void Controller::deactivateStandby() {
    deactivate();
    setMode(MODE_BREW);
}

bool Controller::isActive() const {
    // Use consistent timeout to prevent deadlocks in UI/event loops
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        // For UI/display code: return false on timeout to avoid false positives
        ESP_LOGW(LOG_TAG, "Mutex timeout in isActive - returning false (UI-safe: assume inactive)");
        return false;
    }
    
    Process *proc = currentProcess;
    bool result = proc != nullptr && proc->isActive();
    
    xSemaphoreGive(processMutex);
    return result;
}

bool Controller::isActiveSafe() const {
    // Use consistent timeout to prevent deadlocks in UI/event loops
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        // CRITICAL: Return true on timeout to prevent activate()/onFlush() from calling clear()
        // while a process may actually be running. False negatives are safer than false positives.
        ESP_LOGW(LOG_TAG, "Mutex timeout in isActiveSafe - returning true (conservative: assume active)");
        return true;
    }
    
    Process *proc = currentProcess;
    bool result = proc != nullptr && proc->isActive();
    
    xSemaphoreGive(processMutex);
    return result;
}

bool Controller::isGrindAvailable() const {
    return settings.isSmartGrindActive() || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
}

bool Controller::isManualAvailable() const { return systemInfo.capabilities.pressure; }

bool Controller::isGrindActive() const {
    // Use consistent timeout to prevent deadlocks in UI/event loops
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in isGrindActive - returning false (process may be active)");
        return false;
    }
    
    Process *proc = currentProcess;
    bool result = proc != nullptr && proc->isActive() && proc->getType() == MODE_GRIND;
    
    xSemaphoreGive(processMutex);
    return result;
}

int Controller::getProcessType() const {
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in getProcessType - returning -1");
        return -1;
    }
    
    int type = -1;
    if (currentProcess != nullptr) {
        type = currentProcess->getType();
    }
    
    xSemaphoreGive(processMutex);
    return type;
}

uint8_t Controller::getBrewProcessPhaseIndex() const {
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in getBrewProcessPhaseIndex - returning 0");
        return 0;
    }
    
    uint8_t phaseIndex = 0;
    if (currentProcess != nullptr && currentProcess->getType() == MODE_BREW) {
        auto *brewProcess = static_cast<BrewProcess *>(currentProcess);
        phaseIndex = static_cast<uint8_t>(brewProcess->phaseIndex);
    }
    
    xSemaphoreGive(processMutex);
    return phaseIndex;
}

bool Controller::isBrewProcessVolumetric() const {
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in isBrewProcessVolumetric - returning false");
        return false;
    }
    
    bool isVolumetric = false;
    if (currentProcess != nullptr && currentProcess->getType() == MODE_BREW) {
        auto *brewProcess = static_cast<BrewProcess *>(currentProcess);
        isVolumetric = brewProcess->target == ProcessTarget::VOLUMETRIC &&
                      brewProcess->currentPhase.hasVolumetricTarget() && isVolumetricAvailable();
    }
    
    xSemaphoreGive(processMutex);
    return isVolumetric;
}

bool Controller::isBrewProcessUtility() const {
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in isBrewProcessUtility - returning false");
        return false;
    }
    
    bool isUtility = false;
    if (currentProcess != nullptr && currentProcess->getType() == MODE_BREW) {
        auto *brewProcess = static_cast<BrewProcess *>(currentProcess);
        isUtility = brewProcess->isUtility();
    }
    
    xSemaphoreGive(processMutex);
    return isUtility;
}

ProcessSnapshot Controller::getProcessSnapshot() const {
    ProcessSnapshot snapshot;
    
    // Use consistent timeout strategy to prevent deadlocks
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Mutex timeout in getProcessSnapshot - returning empty snapshot");
        return snapshot;
    }
    
    Process *proc = currentProcess;
    if (proc == nullptr) {
        proc = lastProcess;
    }
    
    if (proc != nullptr) {
        snapshot.exists = true;
        snapshot.isActive = proc->isActive();
        snapshot.isComplete = proc->isComplete();
        snapshot.type = proc->getType();
        // Note: These fields are only available in BrewProcess, not in base Process class
        if (proc->getType() == MODE_BREW) {
            auto *brew = static_cast<BrewProcess *>(proc);
            snapshot.started = brew->processStarted;
            snapshot.finished = brew->finished;
        } else if (proc->getType() == MODE_MANUAL) {
            auto *manual = static_cast<ManualProcess *>(proc);
            snapshot.started = manual->started;
            snapshot.finished = manual->finished;
        } else {
            snapshot.started = 0;
            snapshot.finished = 0;
        }
        
        if (proc->getType() == MODE_BREW) {
            auto *brew = static_cast<BrewProcess *>(proc);
            snapshot.isBrew = true;
            snapshot.phaseIndex = static_cast<uint8_t>(brew->phaseIndex);
            snapshot.phaseName = brew->currentPhase.name;
            snapshot.phaseType = static_cast<int>(brew->currentPhase.phase);
            snapshot.currentPhaseStarted = brew->currentPhaseStarted;
            snapshot.currentVolume = brew->currentVolume;
            snapshot.target = brew->target;
            snapshot.hasVolumetricTarget = brew->currentPhase.hasVolumetricTarget();
            if (snapshot.hasVolumetricTarget) {
                Target t = brew->currentPhase.getVolumetricTarget();
                snapshot.volumetricTargetValue = t.value;
            }
            snapshot.phaseDuration = brew->getPhaseDuration();
            snapshot.phaseCount = brew->profile.phases.size();
            snapshot.totalDuration = brew->getTotalDuration();
            snapshot.brewVolume = brew->getBrewVolume();
            if (brew->processPhase != ProcessPhase::FINISHED) {
                snapshot.isAdvancedPump = brew->isAdvancedPump();
                if (snapshot.isAdvancedPump) {
                    snapshot.pumpPressure = brew->getPumpPressure();
                }
            }
        } else if (proc->getType() == MODE_GRIND) {
            snapshot.isGrind = true;
            auto *grind = static_cast<GrindProcess *>(proc);
            snapshot.target = grind->target;
            snapshot.grindVolume = grind->grindVolume;
            snapshot.grindTime = grind->time;
            snapshot.currentVolume = grind->currentVolume;
        } else if (proc->getType() == MODE_MANUAL) {
            auto *manual = static_cast<ManualProcess *>(proc);
            snapshot.isManual = true;
            snapshot.started = manual->started;
            snapshot.finished = manual->finished;
            snapshot.manualTargetType = manual->targetType;
            snapshot.manualPressure = manual->pressure;
            snapshot.manualFlow = manual->flow;
            snapshot.manualTemperature = manual->temperature;
        }
    }
    
    xSemaphoreGive(processMutex);
    return snapshot;
}

int Controller::getMode() const { return mode; }

void Controller::setMode(int newMode) {
    if (newMode == MODE_GRIND && !isGrindAvailable())
        return;
    if (newMode == MODE_MANUAL && !isManualAvailable())
        return;
    Event modeEvent = pluginManager->trigger("controller:mode:change", "value", newMode);
    mode = modeEvent.getInt("value");
    steamReady = false;

    updateLastAction();
    setTargetTemp(getTargetTemp());
    if (mode == MODE_MANUAL) {
        updateControl();
    }
}

void Controller::onTempRead(float temperature) {
    float temp = temperature - static_cast<float>(settings.getTemperatureOffset());
    Event event = pluginManager->trigger("boiler:currentTemperature:change", "value", temp);
    currentTemp = event.getFloat("value");
}

void Controller::updateLastAction() { lastAction = millis(); }

void Controller::onOTAUpdate() {
    activateStandby();
    updating = true;
}

void Controller::onProfileSave() const { profileManager->saveProfile(profileManager->getSelectedProfile()); }

void Controller::onProfileSaveAsNew() {
    Profile &profile = profileManager->getSelectedProfile();
    profile.label = "Copy of " + profileManager->getSelectedProfile().label;
    profile.id = generateShortID();
    settings.setSelectedProfile(profile.id);
    profileManager->saveProfile(profileManager->getSelectedProfile());
    profileManager->addFavoritedProfile(profile.id);
}

void Controller::onVolumetricMeasurement(double measurement, VolumetricMeasurementSource source) {
    pluginManager->trigger(source == VolumetricMeasurementSource::FLOW_ESTIMATION
                               ? F("controller:volumetric-measurement:estimation:change")
                               : F("controller:volumetric-measurement:bluetooth:change"),
                           "value", static_cast<float>(measurement));
    if (source == VolumetricMeasurementSource::BLUETOOTH) {
        lastBluetoothMeasurement = millis();
    }

    if (currentVolumetricSource != source) {
        ESP_LOGD(LOG_TAG, "Ignoring volumetric measurement, source does not match");
        return;
    }
    
    // Update volume with mutex protection for both currentProcess and lastProcess
    if (xSemaphoreTake(processMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (currentProcess != nullptr) {
            currentProcess->updateVolume(measurement);
        }
        // Also update lastProcess while holding mutex to prevent race with clear()
        if (lastProcess != nullptr && !lastProcess->isComplete()) {
            lastProcess->updateVolume(measurement);
        }
        xSemaphoreGive(processMutex);
    }
}

bool Controller::isBluetoothScaleHealthy() const {
    long timeSinceLastBluetooth = (long)(millis() - lastBluetoothMeasurement);
    return (timeSinceLastBluetooth < BLUETOOTH_GRACE_PERIOD_MS) || volumetricOverride;
}

void Controller::onFlush() {
    if (isActiveSafe()) {
        return;
    }
    clear();
    Profile flushProfile = FLUSH_PROFILE;
    flushProfile.phases[0].duration = settings.getFlushDuration() / 1000.0f;
    startProcess(new BrewProcess(flushProfile, ProcessTarget::TIME, settings.getBrewDelay()));
    pluginManager->trigger("controller:brew:start");
}

void Controller::onVolumetricDelete() {
    if (profileManager->getSelectedProfile().isVolumetric()) {
        profileManager->getSelectedProfile().removeVolumetricTarget();
    }
}

void Controller::handleBrewButton(int brewButtonStatus) {
    ESP_LOGD(LOG_TAG, "current screen %d, brew button %d", getMode(), brewButtonStatus);
    if (brewButtonStatus) {
        switch (getMode()) {
        case MODE_STANDBY:
            deactivateStandby();
            break;
        case MODE_BREW:
            if (!isActiveSafe()) {
                deactivateStandby();
                clear();
                activate();
            } else if (settings.isMomentaryButtons()) {
                deactivate();
                clear();
            }
            break;
        case MODE_WATER:
            activate();
            break;
        case MODE_STEAM:
            deactivate();
            setMode(MODE_BREW);
        default:
            break;
        }
    } else if (!settings.isMomentaryButtons()) {
        if (getMode() == MODE_BREW) {
            if (isActiveSafe()) {
                deactivate();
                clear();
            } else {
                clear();
            }
        } else if (getMode() == MODE_WATER) {
            deactivate();
        }
    }
}

void Controller::handleSteamButton(int steamButtonStatus) {
    ESP_LOGD(LOG_TAG, "current screen %d, steam button %d", getMode(), steamButtonStatus);
    if (steamButtonStatus) {
        switch (getMode()) {
        case MODE_STANDBY:
            setMode(MODE_STEAM);
            break;
        case MODE_BREW:
            setMode(MODE_STEAM);
            break;
        default:
            break;
        }
    } else if (!settings.isMomentaryButtons() && getMode() == MODE_STEAM) {
        deactivate();
        setMode(MODE_BREW);
    }
}

void Controller::handleProfileUpdate() {
    pluginManager->trigger("boiler:targetTemperature:change", "value", profileManager->getSelectedProfile().temperature);
    pluginManager->trigger("controller:targetDuration:change", "value", profileManager->getSelectedProfile().getTotalDuration());
    pluginManager->trigger("controller:targetVolume:change", "value", profileManager->getSelectedProfile().getTotalVolume());
}

void Controller::loopTask(void *arg) {
    TickType_t lastWake = xTaskGetTickCount();
    auto *controller = static_cast<Controller *>(arg);
    while (true) {
        controller->loopControl();
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(controller->getMode() == MODE_STANDBY ? 1000 : 100));
    }
}
