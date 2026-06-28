#include "NimBLEClientController.h"

#include "comms.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <esp_heap_caps.h>

constexpr size_t MAX_CONNECT_RETRIES = 3;

NimBLEClientController::NimBLEClientController() : client(nullptr) {}

void NimBLEClientController::initClient() {
    ESP_LOGI(LOG_TAG, "Pre-BLE-init heap: free=%u largest_block=%u", static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
    NimBLEDevice::init("GPBLC");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Set to maximum power
    NimBLEDevice::setMTU(128);
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
    scanner->setInterval(2000);
    scanner->setWindow(100);
    scanner->setMaxResults(0);
    scanner->setDuplicateFilter(false);
    scanner->setActiveScan(true);
    scanner->start(0, nullptr, false); // Set to 0 for continuous
}

void NimBLEClientController::tare() {
    if (volumetricTareChar != nullptr && client->isConnected()) {
        volumetricTareChar->writeValue("1");
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

    // Clear the ready flag the moment a connect attempt starts so the scan
    // callback / loop() cannot re-enter this path while we are still connecting.
    readyForConnection = false;

    unsigned int tries = 0;
    do {
        if (tries >= MAX_CONNECT_RETRIES) {
            ESP_LOGE(LOG_TAG, "Connection timeout! Unable to connect to BLE server.");
            scan();
            return false; // Exit the connection attempt if timed out
        }

        if (!client->connect(serverAddress)) {
            ESP_LOGE(LOG_TAG, "Failed connecting to BLE server. Retrying...");
            delay(500); // Add a small delay to avoid busy-waiting
        }

        tries++;
    } while (!client->isConnected());
    client->updateConnParams(6, 8, 0, 400);

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

    delay(500);

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
        // PRO-242: nanopb wire format. Byte 0 = type discriminator (1=advanced),
        // bytes 1.. = encoded AdvancedOutput.
        gaggimate_AdvancedOutput msg = gaggimate_AdvancedOutput_init_zero;
        msg.valve = valve;
        msg.boiler_setpoint = boilerSetpoint;
        msg.pressure_target = pressureTarget;
        msg.pump_pressure = pressure;
        msg.pump_flow = flow;

        uint8_t buf[1 + gaggimate_AdvancedOutput_size];
        buf[0] = 1;
        pb_ostream_t os = pb_ostream_from_buffer(buf + 1, sizeof(buf) - 1);
        if (!pb_encode(&os, gaggimate_AdvancedOutput_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendAdvancedOutputControl encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        outputControlChar->writeValue(buf, 1 + os.bytes_written, false);
    }
}

void NimBLEClientController::sendOutputControl(bool valve, float pumpSetpoint, float boilerSetpoint) {
    if (client->isConnected() && outputControlChar != nullptr) {
        // PRO-242: nanopb wire format. Byte 0 = type discriminator (0=simple),
        // bytes 1.. = encoded SimpleOutput.
        gaggimate_SimpleOutput msg = gaggimate_SimpleOutput_init_zero;
        msg.valve = valve;
        msg.pump_setpoint = pumpSetpoint;
        msg.boiler_setpoint = boilerSetpoint;

        uint8_t buf[1 + gaggimate_SimpleOutput_size];
        buf[0] = 0;
        pb_ostream_t os = pb_ostream_from_buffer(buf + 1, sizeof(buf) - 1);
        if (!pb_encode(&os, gaggimate_SimpleOutput_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendOutputControl encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        outputControlChar->writeValue(buf, 1 + os.bytes_written, false);
    }
}

void NimBLEClientController::sendPidSettings(const String &pid) {
    if (pidControlChar != nullptr && client->isConnected()) {
        pidControlChar->writeValue(pid);
    }
}

void NimBLEClientController::sendPumpModelCoeffs(const String &pumpModelCoeffs) {
    if (pumpModelCoeffsChar != nullptr && client->isConnected()) {
        pumpModelCoeffsChar->writeValue(pumpModelCoeffs);
    }
}

void NimBLEClientController::setPressureScale(float scale) {
    if (client->isConnected() && pressureScaleChar != nullptr) {
        pressureScaleChar->writeValue(float_to_string(scale));
    }
}

void NimBLEClientController::sendLedControl(uint8_t channel, uint8_t brightness) {
    if (client->isConnected() && ledControlChar != nullptr) {
        ledControlChar->writeValue(String(channel) + "," + String(brightness));
    }
}

void NimBLEClientController::sendAltControl(bool pinState) {
    if (altControlChar != nullptr && client->isConnected()) {
        altControlChar->writeValue(pinState ? "1" : "0");
    }
}

void NimBLEClientController::sendPing() {
    if (pingChar != nullptr && client->isConnected()) {
        pingChar->writeValue("1");
    }
}

void NimBLEClientController::sendAutotune(int testTime, int samples) {
    if (autotuneChar != nullptr && client->isConnected()) {
        autotuneChar->writeValue(std::to_string(testTime) + "," + std::to_string(samples));
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
            // Copy the address by value; the advertised-device pointer may be
            // freed once the scan-result cache is cleared after stop().
            serverAddress = advertisedDevice->getAddress();
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

// Notification callback
void NimBLEClientController::notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length,
                                            bool) const {
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(ERROR_CHAR_UUID))) {
        // PRO-243: nanopb wire format (was atoi text).
        gaggimate_Error msg = gaggimate_Error_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_Error_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "Error decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Error read: %d", static_cast<int>(msg.code));
        if (remoteErrorCallback != nullptr) {
            remoteErrorCallback(msg.code);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(BREW_BTN_UUID))) {
        // PRO-243: nanopb wire format (was atoi text).
        gaggimate_BrewButton msg = gaggimate_BrewButton_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_BrewButton_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "BrewButton decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "brew button: %d", msg.pressed);
        if (brewBtnCallback != nullptr) {
            brewBtnCallback(msg.pressed);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(STEAM_BTN_UUID))) {
        // PRO-243: nanopb wire format (was atoi text).
        gaggimate_SteamButton msg = gaggimate_SteamButton_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_SteamButton_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "SteamButton decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "steam button: %d", msg.pressed);
        if (steamBtnCallback != nullptr) {
            steamBtnCallback(msg.pressed);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(SENSOR_DATA_UUID))) {
        // PRO-242: nanopb wire format (was lossy 3-dp float_to_string text).
        gaggimate_SensorData msg = gaggimate_SensorData_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_SensorData_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "SensorData decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG,
                 "Received sensor data: temperature=%.1f, pressure=%.1f, puck_flow=%.1f, pump_flow=%.1f, puck_resistance=%.1f",
                 msg.temperature, msg.pressure, msg.puck_flow, msg.pump_flow, msg.puck_resistance);
        if (sensorCallback != nullptr) {
            sensorCallback(msg.temperature, msg.pressure, msg.puck_flow, msg.pump_flow, msg.puck_resistance);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(AUTOTUNE_RESULT_UUID))) {
        // PRO-243: nanopb wire format (was lossy comma text "Kp,Ki,Kd,Kf"). The
        // proto carries Kf too; the controller only ever sends Kp/Ki/Kd (Kf=0),
        // so decoded kf stays 0. Callback signature (Kp,Ki,Kd,Kf) is unchanged.
        gaggimate_AutotuneResult msg = gaggimate_AutotuneResult_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_AutotuneResult_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "AutotuneResult decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "autotune result: Kp=%.4f Ki=%.4f Kd=%.4f Kf=%.4f", msg.kp, msg.ki, msg.kd, msg.kf);
        if (autotuneResultCallback != nullptr) {
            autotuneResultCallback(msg.kp, msg.ki, msg.kd, msg.kf);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(VOLUMETRIC_MEASUREMENT_UUID))) {
        // PRO-243: nanopb wire format (was lossy 3-dp float text via atof).
        gaggimate_VolumetricMeasurement msg = gaggimate_VolumetricMeasurement_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_VolumetricMeasurement_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "VolumetricMeasurement decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Volumetric measurement: %.2f", msg.value);
        if (volumetricMeasurementCallback != nullptr) {
            volumetricMeasurementCallback(msg.value);
        }
    }
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID(TOF_MEASUREMENT_UUID))) {
        // PRO-243: nanopb wire format (was atoi text).
        gaggimate_TofMeasurement msg = gaggimate_TofMeasurement_init_zero;
        pb_istream_t is = pb_istream_from_buffer(pData, length);
        if (!pb_decode(&is, gaggimate_TofMeasurement_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "TofMeasurement decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "ToF measurement: %d", static_cast<int>(msg.distance_mm));
        if (tofMeasurementCallback != nullptr) {
            tofMeasurementCallback(msg.distance_mm);
        }
    }
}

void NimBLEClientController::loopTask(void *arg) {
    TickType_t lastWake = xTaskGetTickCount();
    auto *controller = static_cast<NimBLEClientController *>(arg);
    while (true) {
        controller->loop();
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5000));
    }
}
