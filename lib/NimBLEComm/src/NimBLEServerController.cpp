#include "NimBLEServerController.h"

#include "comms.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <esp_heap_caps.h>

NimBLEServerController::NimBLEServerController() {}

void NimBLEServerController::initServer(const String infoString) {
    this->infoString = infoString;
    ESP_LOGI(LOG_TAG, "Pre-BLE-init heap: free=%u largest_block=%u", static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
    NimBLEDevice::init("GPBLS");
    NimBLEDevice::setPower(9); // +9 dBm. NimBLE 2.x: setPower takes int8_t dBm, not the
                               // esp_power_level_t enum (whose ESP_PWR_LVL_P9 value is 7, not 9). PRO-290.
    NimBLEDevice::setMTU(128);

    // Create BLE Server
    server = NimBLEDevice::createServer();
    server->setCallbacks(this); // Use this class as the callback handler

    // Create BLE Service
    NimBLEService *pService = server->createService(SERVICE_UUID);

    // Output Control Characteristic (Client writes setpoints)
    outputControlChar = pService->createCharacteristic(OUTPUT_CONTROL_UUID, NIMBLE_PROPERTY::WRITE);
    outputControlChar->setCallbacks(this); // Use this class as the callback handler

    // Alt Control Characteristic (Client writes pin state)
    altControlChar = pService->createCharacteristic(ALT_CONTROL_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    altControlChar->setCallbacks(this); // Use this class as the callback handler

    // Ping Characteristic (Client writes ping, Server reads)
    pingChar = pService->createCharacteristic(PING_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    pingChar->setCallbacks(this); // Use this class as the callback handler

    // PID control Characteristic (Client writes PID settings, Server reads)
    pidControlChar = pService->createCharacteristic(PID_CONTROL_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    pidControlChar->setCallbacks(this); // Use this class as the callback handler

    // Pump Model Coefficients Characteristic (Client writes pump model coefficients, Server reads)
    pumpModelCoeffsChar = pService->createCharacteristic(PUMP_MODEL_COEFFS_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    pumpModelCoeffsChar->setCallbacks(this); // Use this class as the callback handler

    // Error Characteristic (Server writes error, Client reads)
    errorChar = pService->createCharacteristic(ERROR_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    // Ping Characteristic (Client writes autotune, Server reads)
    autotuneChar = pService->createCharacteristic(AUTOTUNE_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    autotuneChar->setCallbacks(this); // Use this class as the callback handler
    autotuneResultChar = pService->createCharacteristic(AUTOTUNE_RESULT_UUID, NIMBLE_PROPERTY::NOTIFY);

    // Brew button Characteristic (Server notifies client of brew button)
    brewBtnChar = pService->createCharacteristic(BREW_BTN_UUID, NIMBLE_PROPERTY::NOTIFY);

    // Steam button Characteristic (Server notifies client of steam button)
    steamBtnChar = pService->createCharacteristic(STEAM_BTN_UUID, NIMBLE_PROPERTY::NOTIFY);

    infoChar = pService->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
    setInfo(infoString);

    // Pressure Read Characteristic (Server notifies client of pressure)
    sensorChar = pService->createCharacteristic(SENSOR_DATA_UUID, NIMBLE_PROPERTY::NOTIFY);

    // PID control Characteristic (Client writes pressure settings, Server reads)
    pressureScaleChar = pService->createCharacteristic(PRESSURE_SCALE_UUID, NIMBLE_PROPERTY::WRITE);
    pressureScaleChar->setCallbacks(this); // Use this class as the callback handler

    volumetricMeasurementChar = pService->createCharacteristic(VOLUMETRIC_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
    volumetricTareChar = pService->createCharacteristic(VOLUMETRIC_TARE_UUID, NIMBLE_PROPERTY::WRITE);
    volumetricTareChar->setCallbacks(this);

    tofMeasurementChar = pService->createCharacteristic(TOF_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
    ledControlChar = pService->createCharacteristic(LED_CONTROL_UUID, NIMBLE_PROPERTY::WRITE);
    ledControlChar->setCallbacks(this);

    pService->start();

    ota_dfu_ble.configure_OTA(server);
    ota_dfu_ble.start_OTA();

    advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    // NimBLE 2.x: setScanResponse() renamed to enableScanResponse(); scan
    // response is no longer enabled by default. PRO-290.
    advertising->enableScanResponse(true);
    advertising->start();
    ESP_LOGI(LOG_TAG, "BLE Server started, advertising...\n");
    xTaskCreate(loopTask, "NimBLEServerController::loop", configMINIMAL_STACK_SIZE * 4, this, 1, &taskHandle);
}

void NimBLEServerController::loop() {
    if (server->getConnectedCount() == 0 && !advertising->isAdvertising()) {
        advertising->stop();
        advertising->start();
    }
}

void NimBLEServerController::sendSensorData(float temperature, float pressure, float puckFlow, float pumpFlow,
                                            float puckResistance) {
    if (deviceConnected && sensorChar != nullptr) {
        // PRO-242: nanopb wire format (was lossy 3-dp float_to_string text).
        gaggimate_SensorData msg = gaggimate_SensorData_init_zero;
        msg.temperature = temperature;
        msg.pressure = pressure;
        msg.puck_flow = puckFlow;
        msg.pump_flow = pumpFlow;
        msg.puck_resistance = puckResistance;

        uint8_t buf[gaggimate_SensorData_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_SensorData_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendSensorData encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        sensorChar->setValue(buf, os.bytes_written);
        sensorChar->notify();
    }
}

void NimBLEServerController::sendError(int errorCode) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was std::to_string text).
        gaggimate_Error msg = gaggimate_Error_init_zero;
        msg.code = errorCode;

        uint8_t buf[gaggimate_Error_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_Error_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendError encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        errorChar->setValue(buf, os.bytes_written);
        errorChar->notify();
    }
}

void NimBLEServerController::sendBrewBtnState(bool brewButtonStatus) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was std::to_string text).
        gaggimate_BrewButton msg = gaggimate_BrewButton_init_zero;
        msg.pressed = brewButtonStatus;

        uint8_t buf[gaggimate_BrewButton_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_BrewButton_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendBrewBtnState encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        brewBtnChar->setValue(buf, os.bytes_written);
        brewBtnChar->notify();
    }
}

void NimBLEServerController::sendSteamBtnState(bool steamButtonStatus) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was std::to_string text).
        gaggimate_SteamButton msg = gaggimate_SteamButton_init_zero;
        msg.pressed = steamButtonStatus;

        uint8_t buf[gaggimate_SteamButton_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_SteamButton_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendSteamBtnState encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        steamBtnChar->setValue(buf, os.bytes_written);
        steamBtnChar->notify();
    }
}

void NimBLEServerController::sendAutotuneResult(float Kp, float Ki, float Kd) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was lossy 3-dp float_to_string text with a
        // "0.0" Kf token appended). Kf stays at its proto default (0 = disabled);
        // the public 3-float signature is unchanged.
        gaggimate_AutotuneResult msg = gaggimate_AutotuneResult_init_zero;
        msg.kp = Kp;
        msg.ki = Ki;
        msg.kd = Kd;

        uint8_t buf[gaggimate_AutotuneResult_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_AutotuneResult_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendAutotuneResult encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        autotuneResultChar->setValue(buf, os.bytes_written);
        autotuneResultChar->notify();
    }
}

void NimBLEServerController::sendVolumetricMeasurement(float value) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was lossy 3-dp float_to_string text).
        gaggimate_VolumetricMeasurement msg = gaggimate_VolumetricMeasurement_init_zero;
        msg.value = value;

        uint8_t buf[gaggimate_VolumetricMeasurement_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_VolumetricMeasurement_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendVolumetricMeasurement encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        volumetricMeasurementChar->setValue(buf, os.bytes_written);
        volumetricMeasurementChar->notify();
    }
}

void NimBLEServerController::sendTofMeasurement(int value) {
    if (deviceConnected) {
        // PRO-243: nanopb wire format (was std::to_string text).
        gaggimate_TofMeasurement msg = gaggimate_TofMeasurement_init_zero;
        msg.distance_mm = value;

        uint8_t buf[gaggimate_TofMeasurement_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        if (!pb_encode(&os, gaggimate_TofMeasurement_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "sendTofMeasurement encode failed: %s", PB_GET_ERROR(&os));
            return;
        }
        tofMeasurementChar->setValue(buf, os.bytes_written);
        tofMeasurementChar->notify();
    }
}

void NimBLEServerController::registerOutputControlCallback(const simple_output_callback_t &callback) {
    outputControlCallback = callback;
}

void NimBLEServerController::registerAdvancedOutputControlCallback(const advanced_output_callback_t &callback) {
    advancedControlCallback = callback;
}

void NimBLEServerController::registerAltControlCallback(const pin_control_callback_t &callback) { altControlCallback = callback; }
void NimBLEServerController::registerPingCallback(const ping_callback_t &callback) { pingCallback = callback; }
void NimBLEServerController::registerAutotuneCallback(const autotune_callback_t &callback) { autotuneCallback = callback; }
void NimBLEServerController::registerPressureScaleCallback(const float_callback_t &callback) { pressureScaleCallback = callback; }

void NimBLEServerController::registerTareCallback(const void_callback_t &callback) { tareCallback = callback; }

void NimBLEServerController::registerLedControlCallback(const led_control_callback_t &callback) { ledControlCallback = callback; }

void NimBLEServerController::setInfo(const String infoString) {
    this->infoString = infoString;
    // PRO-243: infoString carries the raw nanopb SystemInfo bytes (built by
    // make_system_info). Write via the length-delimited buffer overload so any
    // embedded NUL bytes in the protobuf payload survive (a String overload
    // could stop at the first NUL).
    infoChar->setValue(reinterpret_cast<const uint8_t *>(infoString.c_str()), infoString.length());
}

void NimBLEServerController::registerPidControlCallback(const pid_control_callback_t &callback) { pidControlCallback = callback; }

void NimBLEServerController::registerPumpModelCoeffsCallback(const pump_model_coeffs_callback_t &callback) {
    pumpModelCoeffsCallback = callback;
}

// NimBLEServerCallbacks override (NimBLE 2.x signature: + NimBLEConnInfo&)
void NimBLEServerController::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
    ESP_LOGI(LOG_TAG, "Client connected.");
    deviceConnected = true;
    pServer->stopAdvertising();
}

// NimBLE 2.x no longer auto-restarts advertising on disconnect; this manual
// startAdvertising() (preserved from 1.x) keeps reconnects working. Signature
// gained NimBLEConnInfo& and an int reason in 2.x. PRO-290.
void NimBLEServerController::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) {
    ESP_LOGI(LOG_TAG, "Client disconnected.");
    deviceConnected = false;
    pServer->startAdvertising(); // Restart advertising so clients can reconnect
}

void NimBLEServerController::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
    ESP_LOGV(LOG_TAG, "Write received!");

    if (pCharacteristic->getUUID().equals(NimBLEUUID(OUTPUT_CONTROL_UUID))) {
        // PRO-242: nanopb wire format. Byte 0 = type discriminator (0 simple /
        // 1 advanced), bytes 1.. = the encoded SimpleOutput / AdvancedOutput.
        const NimBLEAttValue raw = pCharacteristic->getValue();
        if (raw.size() < 1) {
            ESP_LOGW(LOG_TAG, "Output control payload too short");
            return;
        }
        const uint8_t type = raw[0];
        const uint8_t *body = raw.data() + 1;
        const size_t bodyLen = raw.size() - 1;
        if (type == 0) {
            gaggimate_SimpleOutput msg = gaggimate_SimpleOutput_init_zero;
            pb_istream_t is = pb_istream_from_buffer(body, bodyLen);
            if (!pb_decode(&is, gaggimate_SimpleOutput_fields, &msg)) {
                ESP_LOGE(LOG_TAG, "SimpleOutput decode failed: %s", PB_GET_ERROR(&is));
                return;
            }
            ESP_LOGV(LOG_TAG, "Received output control: type=%d, valve=%d, pump=%.1f, boiler=%.1f", type, msg.valve,
                     msg.pump_setpoint, msg.boiler_setpoint);
            if (outputControlCallback != nullptr) {
                outputControlCallback(msg.valve, msg.pump_setpoint, msg.boiler_setpoint);
            }
        } else if (type == 1) {
            gaggimate_AdvancedOutput msg = gaggimate_AdvancedOutput_init_zero;
            pb_istream_t is = pb_istream_from_buffer(body, bodyLen);
            if (!pb_decode(&is, gaggimate_AdvancedOutput_fields, &msg)) {
                ESP_LOGE(LOG_TAG, "AdvancedOutput decode failed: %s", PB_GET_ERROR(&is));
                return;
            }
            ESP_LOGV(LOG_TAG, "Received advanced output control: type=%d, valve=%d, pressure_target=%d, pressure=%.1f, flow=%.1f",
                     type, msg.valve, msg.pressure_target, msg.pump_pressure, msg.pump_flow);
            if (advancedControlCallback != nullptr) {
                advancedControlCallback(msg.valve, msg.boiler_setpoint, msg.pressure_target, msg.pump_pressure, msg.pump_flow);
            }
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(ALT_CONTROL_CHAR_UUID))) {
        // PRO-244: nanopb wire format (was raw '1'/'0' byte).
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_AltControl msg = gaggimate_AltControl_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_AltControl_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "AltControl decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received ALT control: %s", msg.active ? "ON" : "OFF");
        if (altControlCallback != nullptr) {
            altControlCallback(msg.active);
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(PING_CHAR_UUID))) {
        // PRO-244: nanopb wire format (was literal "1"). Empty message — decode
        // validates the (0-byte) payload before firing the callback.
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_Ping msg = gaggimate_Ping_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_Ping_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "Ping decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received ping");
        if (pingCallback != nullptr) {
            pingCallback();
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(AUTOTUNE_CHAR_UUID))) {
        // PRO-244: nanopb wire format (was "testTime,samples" comma text).
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_AutotuneRequest msg = gaggimate_AutotuneRequest_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_AutotuneRequest_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "AutotuneRequest decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received autotune: test_time=%d, samples=%d", static_cast<int>(msg.test_time),
                 static_cast<int>(msg.samples));
        if (autotuneCallback != nullptr) {
            autotuneCallback(msg.test_time, msg.samples);
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(PID_CONTROL_CHAR_UUID))) {
        // PRO-244: nanopb wire format (was "Kp,Ki,Kd[,Kf]" comma text). Kf is
        // optional on the wire; when the display omits it the proto default 0
        // mirrors the old "Kf=0 when unset" behavior. Callback stays 4-arg.
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_PidSettings msg = gaggimate_PidSettings_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_PidSettings_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "PidSettings decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGI(LOG_TAG, "Parsed PID: Kp=%.2f, Ki=%.2f, Kd=%.2f, Kf=%.3f (combined)", msg.kp, msg.ki, msg.kd, msg.kf);
        if (pidControlCallback != nullptr) {
            pidControlCallback(msg.kp, msg.ki, msg.kd, msg.kf);
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(PUMP_MODEL_COEFFS_CHAR_UUID))) {
        // PRO-244: nanopb wire format (was "a,b,c,d" comma text). c and d may be
        // NaN (two-point flow mode); the IEEE-754 float on the wire preserves it.
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_PumpModelCoeffs msg = gaggimate_PumpModelCoeffs_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_PumpModelCoeffs_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "PumpModelCoeffs decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received pump flow polynomial coefficients: %.6f, %.6f, %.6f, %.6f", msg.a, msg.b, msg.c, msg.d);
        if (pumpModelCoeffsCallback != nullptr) {
            pumpModelCoeffsCallback(msg.a, msg.b, msg.c, msg.d);
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(PRESSURE_SCALE_UUID))) {
        // PRO-244: nanopb wire format (was lossy 3-dp float text).
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_PressureScale msg = gaggimate_PressureScale_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_PressureScale_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "PressureScale decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received pressure scale: %.2f", msg.scale);
        if (pressureScaleCallback != nullptr) {
            pressureScaleCallback(msg.scale);
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(VOLUMETRIC_TARE_UUID))) {
        // PRO-244: nanopb wire format (was literal "1"). Empty message — decode
        // validates the (0-byte) payload before firing the callback.
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_Tare msg = gaggimate_Tare_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_Tare_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "Tare decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received tare");
        if (tareCallback != nullptr) {
            tareCallback();
        }
    } else if (pCharacteristic->getUUID().equals(NimBLEUUID(LED_CONTROL_UUID))) {
        // PRO-244: nanopb wire format (was "channel,brightness" comma text).
        const NimBLEAttValue raw = pCharacteristic->getValue();
        gaggimate_LedControl msg = gaggimate_LedControl_init_zero;
        pb_istream_t is = pb_istream_from_buffer(raw.data(), raw.size());
        if (!pb_decode(&is, gaggimate_LedControl_fields, &msg)) {
            ESP_LOGE(LOG_TAG, "LedControl decode failed: %s", PB_GET_ERROR(&is));
            return;
        }
        ESP_LOGV(LOG_TAG, "Received led control, %d: %d", static_cast<int>(msg.channel), static_cast<int>(msg.brightness));
        if (ledControlCallback != nullptr) {
            ledControlCallback(static_cast<uint8_t>(msg.channel), static_cast<uint8_t>(msg.brightness));
        }
    }
}

void NimBLEServerController::loopTask(void *arg) {
    TickType_t lastWake = xTaskGetTickCount();
    auto *controller = static_cast<NimBLEServerController *>(arg);
    while (true) {
        controller->loop();
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5000));
    }
}
