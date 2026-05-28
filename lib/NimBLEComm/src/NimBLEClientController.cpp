#include "NimBLEClientController.h"

#include "BondPolicy.h"

constexpr size_t MAX_CONNECT_RETRIES = 3;
constexpr size_t MAX_SECURE_CONNECTION_ATTEMPTS = 2;

NimBLEClientController::NimBLEClientController() : client(nullptr) {}

void NimBLEClientController::initClient() {
    NimBLEDevice::init("GPBLC");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Set to maximum power
    NimBLEDevice::setMTU(128);
    NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*MITM*/ false, /*SC*/ true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    client = NimBLEDevice::createClient();
    scanner = NimBLEDevice::getScan();
    if (client == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to create BLE client");
        return;
    }
    client->setClientCallbacks(this);

    // Scan for BLE Server
    scan();
    xTaskCreate(loopTask, "NimBLEClientController::loop", configMINIMAL_STACK_SIZE * 4, this, 1, &taskHandle);
}

void NimBLEClientController::scan() {
    readyForConnection = false;
    scanner->clearDuplicateCache();
    scanner->setAdvertisedDeviceCallbacks(this, true);
    // Use a 50% duty cycle (200 ms interval / 100 ms window) so the scanner
    // sees the controller's advertising packets quickly.  The previous 5%
    // duty cycle (2000 ms / 100 ms) caused extremely slow discovery and
    // frequent reconnection failures.
    scanner->setInterval(200);
    scanner->setWindow(100);
    scanner->setMaxResults(0);
    scanner->setDuplicateFilter(false);
    scanner->setActiveScan(true);
    scanner->start(0, nullptr, false); // Set to 0 for continuous
}

void NimBLEClientController::tare() {
    if (volumetricTareChar != nullptr && client->isConnected()) {
        writeRemoteValue(volumetricTareChar, String("1"));
    }
}

void NimBLEClientController::registerRemoteErrorCallback(const remote_err_callback_t &callback) {
    remoteErrorCallback = callback;
}
void NimBLEClientController::registerBrewBtnCallback(const brew_callback_t &callback) { brewBtnCallback = callback; }
void NimBLEClientController::registerSteamBtnCallback(const brew_callback_t &callback) { steamBtnCallback = callback; }

void NimBLEClientController::registerSensorCallback(const sensor_read_callback_t &callback) { sensorCallback = callback; }

void NimBLEClientController::registerAutotuneResultCallback(const pid_control_callback_t &callback) {
    autotuneResultCallback = callback;
}

void NimBLEClientController::registerVolumetricMeasurementCallback(const float_callback_t &callback) {
    volumetricMeasurementCallback = callback;
}

void NimBLEClientController::registerTofMeasurementCallback(const int_callback_t &callback) { tofMeasurementCallback = callback; }

void NimBLEClientController::registerDisconnectCallback(const void_callback_t &callback) { disconnectCallback = callback; }

std::string NimBLEClientController::readInfo() const {
    if (infoChar != nullptr && infoChar->canRead()) {
        return infoChar->readValue();
    }
    return "";
}

bool NimBLEClientController::connectToServer() {
    ESP_LOGI(LOG_TAG, "Connecting to advertised device");

    unsigned int tries = 0;
    do {
        if (tries >= MAX_CONNECT_RETRIES) {
            ESP_LOGE(LOG_TAG, "Connection timeout! Unable to connect to BLE server.");
            scan();
            return false; // Exit the connection attempt if timed out
        }

        if (!client->connect(NimBLEAddress(serverDevice->getAddress()))) {
            ESP_LOGE(LOG_TAG, "Failed connecting to BLE server. Retrying...");
            vTaskDelay(pdMS_TO_TICKS(500)); // Yield to FreeRTOS instead of spinning
        }

        tries++;
    } while (!client->isConnected());
    // 12,24 = 15-30 ms connection interval; supervisionTimeout=800 = 8 s.
    // The previous 6,8,0,400 (7.5-10 ms / 4 s) was too tight for the EMI-rich
    // environment around an espresso machine (pump SSR, boiler triac, Wi-Fi
    // crowding) and caused mid-session supervision-timeout disconnects.
    // See CAR-231.
    client->updateConnParams(12, 24, 0, 800);

    // Best-effort encryption. The server's control characteristics are plain WRITE,
    // so a failed secureConnection() must NOT block writes — gating on encryption
    // silently rejected the boiler setpoint and the machine never heated. Try once
    // for forward-compat with future encrypted characteristics, log on failure,
    // and continue. The bond-wipe recovery path is intentionally not invoked here
    // anymore; factoryResetBonds() (serial 'B' trigger) remains available for
    // manual recovery if a future char re-introduces encryption.
    bool secure = false;
    for (size_t attempt = 1; attempt <= MAX_SECURE_CONNECTION_ATTEMPTS; ++attempt) {
        if (!client->isConnected()) {
            break;
        }
        secure = client->secureConnection();
        if (secure) {
            break;
        }
        if (attempt < MAX_SECURE_CONNECTION_ATTEMPTS) {
            ESP_LOGW(LOG_TAG, "secureConnection() failed; retrying once");
            delay(250);
        }
    }
    if (!secure) {
        ESP_LOGW(LOG_TAG, "secureConnection() did not encrypt link; continuing with unencrypted writes");
    }
    bleAuthFailed = false;

    ESP_LOGI(LOG_TAG, "Successfully connected to BLE server");

    // Obtain the remote service we wish to connect to
    NimBLERemoteService *pRemoteService = client->getService(NimBLEUUID(SERVICE_UUID));
    if (pRemoteService == nullptr) {
        ESP_LOGE(LOG_TAG, "Error getting remote service");
        scan();
        return false;
    }

    // Obtain the remote write characteristics
    outputControlChar = pRemoteService->getCharacteristic(NimBLEUUID(OUTPUT_CONTROL_UUID));
    altControlChar = pRemoteService->getCharacteristic(NimBLEUUID(ALT_CONTROL_CHAR_UUID));
    autotuneChar = pRemoteService->getCharacteristic(NimBLEUUID(AUTOTUNE_CHAR_UUID));
    pingChar = pRemoteService->getCharacteristic(NimBLEUUID(PING_CHAR_UUID));
    pidControlChar = pRemoteService->getCharacteristic(NimBLEUUID(PID_CONTROL_CHAR_UUID));
    pumpModelCoeffsChar = pRemoteService->getCharacteristic(NimBLEUUID(PUMP_MODEL_COEFFS_CHAR_UUID));
    infoChar = pRemoteService->getCharacteristic(NimBLEUUID(INFO_UUID));
    pressureScaleChar = pRemoteService->getCharacteristic(NimBLEUUID(PRESSURE_SCALE_UUID));
    volumetricTareChar = pRemoteService->getCharacteristic(NimBLEUUID(VOLUMETRIC_TARE_UUID));
    ledControlChar = pRemoteService->getCharacteristic(NimBLEUUID(LED_CONTROL_UUID));

    // Obtain the remote notify characteristic and subscribe to it

    errorChar = pRemoteService->getCharacteristic(NimBLEUUID(ERROR_CHAR_UUID));
    if (errorChar != nullptr && errorChar->canNotify()) {
        errorChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                             std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    brewBtnChar = pRemoteService->getCharacteristic(NimBLEUUID(BREW_BTN_UUID));
    if (brewBtnChar != nullptr && brewBtnChar->canNotify()) {
        brewBtnChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                               std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    steamBtnChar = pRemoteService->getCharacteristic(NimBLEUUID(STEAM_BTN_UUID));
    if (steamBtnChar != nullptr && steamBtnChar->canNotify()) {
        steamBtnChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                                std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    autotuneResultChar = pRemoteService->getCharacteristic(NimBLEUUID(AUTOTUNE_RESULT_UUID));
    if (autotuneResultChar != nullptr && autotuneResultChar->canNotify()) {
        autotuneResultChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                                      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    sensorChar = pRemoteService->getCharacteristic(NimBLEUUID(SENSOR_DATA_UUID));
    if (sensorChar != nullptr && sensorChar->canNotify()) {
        sensorChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                              std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    volumetricMeasurementChar = pRemoteService->getCharacteristic(NimBLEUUID(VOLUMETRIC_MEASUREMENT_UUID));
    if (volumetricMeasurementChar != nullptr && volumetricMeasurementChar->canNotify()) {
        volumetricMeasurementChar->subscribe(true,
                                             std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                                       std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    tofMeasurementChar = pRemoteService->getCharacteristic(NimBLEUUID(TOF_MEASUREMENT_UUID));
    if (tofMeasurementChar != nullptr && tofMeasurementChar->canNotify()) {
        tofMeasurementChar->subscribe(true, std::bind(&NimBLEClientController::notifyCallback, this, std::placeholders::_1,
                                                      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // Yield to FreeRTOS instead of spinning

    readyForConnection = false;
    return true;
}

void NimBLEClientController::loop() {
    if (!readyForConnection && !client->isConnected() && !scanner->isScanning()) {
        ESP_LOGI("NimBLEClientController", "Scan interrupted. Restarting...");
        scan();
    }
}

void NimBLEClientController::sendAdvancedOutputControl(bool valve, float boilerSetpoint, bool pressureTarget, float pressure,
                                                       float flow) {
    if (client->isConnected() && outputControlChar != nullptr) {
        const std::string value = "1," + std::to_string(valve ? 1 : 0) + ",100.0," + std::to_string(boilerSetpoint) + "," +
                                  std::to_string(pressureTarget ? 1 : 0) + "," + float_to_string(pressure) + "," +
                                  float_to_string(flow);
        _lastOutputControl = String(value.c_str());
        writeRemoteValue(outputControlChar, _lastOutputControl, false);
    }
}

void NimBLEClientController::sendOutputControl(bool valve, float pumpSetpoint, float boilerSetpoint) {
    if (client->isConnected() && outputControlChar != nullptr) {
        const std::string value =
            "0," + std::to_string(valve ? 1 : 0) + "," + std::to_string(pumpSetpoint) + "," + std::to_string(boilerSetpoint);
        _lastOutputControl = String(value.c_str());
        writeRemoteValue(outputControlChar, _lastOutputControl, false);
    }
}

void NimBLEClientController::sendPidSettings(const String &pid) {
    if (pidControlChar != nullptr && client->isConnected()) {
        writeRemoteValue(pidControlChar, pid);
    }
}

void NimBLEClientController::sendPumpModelCoeffs(const String &pumpModelCoeffs) {
    if (pumpModelCoeffsChar != nullptr && client->isConnected()) {
        writeRemoteValue(pumpModelCoeffsChar, pumpModelCoeffs);
    }
}

void NimBLEClientController::setPressureScale(float scale) {
    if (client->isConnected() && pressureScaleChar != nullptr) {
        writeRemoteValue(pressureScaleChar, float_to_string(scale));
    }
}

void NimBLEClientController::sendLedControl(uint8_t channel, uint8_t brightness) {
    if (client->isConnected() && ledControlChar != nullptr) {
        writeRemoteValue(ledControlChar, String(channel) + "," + String(brightness));
    }
}

void NimBLEClientController::sendAltControl(bool pinState) {
    if (altControlChar != nullptr && client->isConnected()) {
        writeRemoteValue(altControlChar, String(pinState ? "1" : "0"));
    }
}

void NimBLEClientController::sendPing() {
    if (pingChar != nullptr && client->isConnected()) {
        writeRemoteValue(pingChar, String("1"));
    }
}

void NimBLEClientController::sendAutotune(int testTime, int samples) {
    if (autotuneChar != nullptr && client->isConnected()) {
        writeRemoteValue(autotuneChar, std::to_string(testTime) + "," + std::to_string(samples));
    }
}

bool NimBLEClientController::isReadyForConnection() const { return readyForConnection; }

bool NimBLEClientController::isConnected() { return client != nullptr && client->isConnected(); }

// BLEAdvertisedDeviceCallbacks override
void NimBLEClientController::onResult(NimBLEAdvertisedDevice *advertisedDevice) {
    ESP_LOGV(LOG_TAG, "Advertised Device found: %s \n", advertisedDevice->toString().c_str());

    // Check if this is the device we're looking for
    if (advertisedDevice->haveServiceUUID()) {
        ESP_LOGI(LOG_TAG, "Found BLE service. Checking for ID...");
        if (advertisedDevice->isAdvertisingService(NimBLEUUID(SERVICE_UUID))) {
            ESP_LOGI(LOG_TAG, "Found target BLE device. Connecting...");
            scanner->stop();
            serverDevice = advertisedDevice;
            readyForConnection = true;
        }
    }
}

void NimBLEClientController::onDisconnect(NimBLEClient *pServer) {
    ESP_LOGI(LOG_TAG, "Disconnected from server, trying to reconnect...");
    tempControlChar = nullptr;
    pumpControlChar = nullptr;
    valveControlChar = nullptr;
    altControlChar = nullptr;
    tempReadChar = nullptr;
    pingChar = nullptr;
    pidControlChar = nullptr;
    pumpModelCoeffsChar = nullptr;
    errorChar = nullptr;
    autotuneChar = nullptr;
    autotuneResultChar = nullptr;
    brewBtnChar = nullptr;
    steamBtnChar = nullptr;
    infoChar = nullptr;
    sensorChar = nullptr;
    outputControlChar = nullptr;
    pressureScaleChar = nullptr;
    volumetricMeasurementChar = nullptr;
    volumetricTareChar = nullptr;
    ledControlChar = nullptr;
    tofMeasurementChar = nullptr;
    if (disconnectCallback != nullptr) {
        disconnectCallback();
    }
    scan();
}

void NimBLEClientController::onAuthenticationComplete(ble_gap_conn_desc *desc) {
    if (desc == nullptr) {
        ESP_LOGW(LOG_TAG, "Auth complete with null desc; marking auth failed");
        bleAuthFailed = true;
        return;
    }
    if (!desc->sec_state.encrypted) {
        ESP_LOGW(LOG_TAG, "Auth complete but link not encrypted; marking auth failed");
        bleAuthFailed = true;
        return;
    }
    ESP_LOGI(LOG_TAG, "BLE link authenticated and encrypted");
    bleAuthFailed = false;
}

bool NimBLEClientController::writeRemoteValue(NimBLERemoteCharacteristic *characteristic, const String &value, bool response) {
    const bool ok = characteristic != nullptr && characteristic->writeValue(value, response);
    bleAuthFailed = ok ? false : bleAuthFailed || isConnected();
    return ok;
}

bool NimBLEClientController::writeRemoteValue(NimBLERemoteCharacteristic *characteristic, const std::string &value, bool response) {
    const bool ok = characteristic != nullptr && characteristic->writeValue(value, response);
    bleAuthFailed = ok ? false : bleAuthFailed || isConnected();
    return ok;
}

void NimBLEClientController::factoryResetBonds() {
    ESP_LOGW(LOG_TAG, "Factory-resetting BLE bonds");
    NimBLEDevice::deleteAllBonds();
    bleAuthFailed = false;
    if (client != nullptr && client->isConnected()) {
        client->disconnect();
    }
    scan();
}

// Notification callback
void NimBLEClientController::notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length,
                                            bool) const {
    std::string rawData((char *)pData, length);

    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(ERROR_CHAR_UUID))) {
        int errorCode = atoi(rawData.c_str());
        ESP_LOGV(LOG_TAG, "Error read: %d", errorCode);
        if (remoteErrorCallback != nullptr) {
            remoteErrorCallback(errorCode);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(BREW_BTN_UUID))) {
        int brewButtonStatus = atoi(rawData.c_str());
        ESP_LOGV(LOG_TAG, "brew button: %d", brewButtonStatus);
        if (brewBtnCallback != nullptr) {
            brewBtnCallback(brewButtonStatus);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(STEAM_BTN_UUID))) {
        int steamButtonStatus = atoi(rawData.c_str());
        ESP_LOGV(LOG_TAG, "steam button: %d", steamButtonStatus);
        if (steamBtnCallback != nullptr) {
            steamBtnCallback(steamButtonStatus);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(SENSOR_DATA_UUID))) {
        String data = String(rawData.c_str());
        float temperature = get_token(data, 0, ',').toFloat();
        float pressure = get_token(data, 1, ',').toFloat();
        float puckFlow = get_token(data, 2, ',').toFloat();
        float pumpFlow = get_token(data, 3, ',').toFloat();
        float puckResistance = get_token(data, 4, ',').toFloat();

        ESP_LOGV(LOG_TAG,
                 "Received sensor data: temperature=%.1f, pressure=%.1f, puck_flow=%.1f, pump_flow=%.1f, puck_resistance=%.1f",
                 temperature, pressure, puckFlow, pumpFlow, puckResistance);
        if (sensorCallback != nullptr) {
            sensorCallback(temperature, pressure, puckFlow, pumpFlow, puckResistance);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(AUTOTUNE_RESULT_UUID))) {
        String settings = String(rawData.c_str());
        ESP_LOGV(LOG_TAG, "autotune result: %s", settings.c_str());
        if (autotuneResultCallback != nullptr) {
            float Kp = get_token(settings, 0, ',').toFloat();
            float Ki = get_token(settings, 1, ',').toFloat();
            float Kd = get_token(settings, 2, ',').toFloat();

            // Handle optional Kf parameter with default
            float Kf = 0.0f; // Default combined Kff
            String kfToken = get_token(settings, 3, ',');
            if (kfToken.length() > 0)
                Kf = kfToken.toFloat();

            autotuneResultCallback(Kp, Ki, Kd, Kf);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(VOLUMETRIC_MEASUREMENT_UUID))) {
        float value = atof(rawData.c_str());
        ESP_LOGV(LOG_TAG, "Volumetric measurement: %.2f", value);
        if (volumetricMeasurementCallback != nullptr) {
            volumetricMeasurementCallback(value);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(TOF_MEASUREMENT_UUID))) {
        int value = atoi(rawData.c_str());
        ESP_LOGV(LOG_TAG, "ToF measurement: %d", value);
        if (tofMeasurementCallback != nullptr) {
            tofMeasurementCallback(value);
        }
    }
}

void NimBLEClientController::loopTask(void *arg) {
    TickType_t lastWake = xTaskGetTickCount();
    auto *controller = static_cast<NimBLEClientController *>(arg);
    while (true) {
        controller->loop();
        // Tick every 1 s so an interrupted scan is restarted within 1 s.
        // The previous 5 s interval caused multi-second blind spots after any
        // scan disruption.
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
    }
}
