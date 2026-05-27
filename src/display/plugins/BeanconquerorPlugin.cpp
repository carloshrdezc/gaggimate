#include "BeanconquerorPlugin.h"
#include "../core/Controller.h"
#include "../core/PluginManager.h"
#include <NimBLEDevice.h>
#include <cstring>

BeanconquerorPlugin::BeanconquerorPlugin() = default;

void BeanconquerorPlugin::setup(Controller *ctrl, PluginManager *manager) {
    controller = ctrl;
    pluginManager = manager;

    // Initialise the GATT server only after the BLE client has successfully
    // connected to the controller board.  Starting it earlier (on
    // controller:bluetooth:init) forces the NimBLE stack to run both a scanner
    // and an advertiser simultaneously; the two compete for RF time and the
    // scanner misses most controller advertising packets, causing persistent
    // connection failures and slow reconnection.
    manager->on("controller:bluetooth:connect", [this](Event const &) {
        if (!bleReady) {
            initBLEServer();
        } else {
            resumeAdvertising();
        }
    });

    // While the display is scanning/reconnecting to the controller, pause
    // advertising so the radio can focus on the scan.
    manager->on("controller:bluetooth:disconnect", [this](Event const &) {
        pauseAdvertising();
    });

    manager->on("controller:brew:start", [this](Event const &) { brewing = true; });
    manager->on("controller:brew:prestart", [this](Event const &) { brewing = true; });
    manager->on("controller:brew:end",   [this](Event const &) { brewing = false; });
    manager->on("controller:brew:clear", [this](Event const &) { brewing = false; });
    manager->on("controller:mode:change", [this](Event const &event) {
        if (event.getInt("value") == 0) brewing = false;
    });
}

void BeanconquerorPlugin::initBLEServer() {
    // Stop the active BLE scan before calling createServer(). createServer()
    // calls ble_gatts_reset() internally; running it while the scanner is
    // active corrupts shared NimBLE host state and triggers a watchdog reboot.
    // NimBLEClientController::loop() will restart the scan automatically.
    NimBLEScan *activeScan = NimBLEDevice::getScan();
    if (activeScan && activeScan->isScanning()) {
        activeScan->stop();
    }

    // NimBLE was already initialized by NimBLEClientController; set the GAP
    // name explicitly so Beanconqueror's scanner finds "ESPROFILE".
    NimBLEDevice::setDeviceName("ESPROFILE");

    server = NimBLEDevice::createServer();

    NimBLEService *pService = server->createService(BC_SERVICE_UUID);
    if (!pService) {
        ESP_LOGE("BeanconquerorPlugin", "Failed to create BLE service");
        return;
    }

    shotDataChar = pService->createCharacteristic(
        BC_SHOT_DATA_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) {
        ESP_LOGE("BeanconquerorPlugin", "Failed to get BLE advertising handle");
        return;
    }
    pAdvertising->addServiceUUID(BC_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    bleReady = true;
    advertisingActive = true;
    ESP_LOGI("BeanconquerorPlugin", "BLE peripheral ready, advertising as ESPROFILE");
}

void BeanconquerorPlugin::pauseAdvertising() {
    if (!bleReady || !advertisingActive) return;
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising && pAdvertising->isAdvertising()) {
        pAdvertising->stop();
        advertisingActive = false;
        ESP_LOGI("BeanconquerorPlugin", "Advertising paused for BLE reconnection scan");
    }
}

void BeanconquerorPlugin::resumeAdvertising() {
    if (!bleReady || advertisingActive) return;
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->start();
        advertisingActive = true;
        ESP_LOGI("BeanconquerorPlugin", "Advertising resumed after controller reconnect");
    }
}

void BeanconquerorPlugin::loop() {
    if (!bleReady || shotDataChar == nullptr) return;
    if (shotDataChar->getSubscribedCount() == 0) return;

    const unsigned long interval = brewing ? BC_NOTIFY_INTERVAL_BREW_MS
                                           : BC_NOTIFY_INTERVAL_IDLE_MS;
    const unsigned long now = millis();
    if (now - lastNotify < interval) return;
    lastNotify = now;

    buildAndNotify();
}

void BeanconquerorPlugin::buildAndNotify() {
    // 18-byte CoffeeSensor / ESPROFILE payload (little-endian):
    //  [0]  uint32 timestamp_ms
    //  [4]  float  probe_temperature_c
    //  [8]  float  pressure_bar_absolute  (gauge + 0.98 bar atmospheric)
    //  [12] float  board_temperature_c
    //  [16] uint8  battery_percent
    //  [17] uint8  flags

    uint8_t payload[18] = {};

    const uint32_t ts = static_cast<uint32_t>(millis());
    memcpy(payload + 0, &ts, 4);

    const float temperature = controller->getCurrentTemp();
    memcpy(payload + 4, &temperature, 4);

    // Beanconqueror subtracts 0.98 bar atmospheric on its side, so send absolute pressure
    const float pressureAbsolute = controller->getCurrentPressure() + 0.98f;
    memcpy(payload + 8, &pressureAbsolute, 4);

    memcpy(payload + 12, &temperature, 4);  // board temp = probe temp

    payload[16] = 100;  // battery%: always full (mains-powered)
    payload[17] = 0;

    shotDataChar->setValue(payload, sizeof(payload));
    shotDataChar->notify();
}
