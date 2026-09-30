#include "GaggiMateController.h"
#include "LinkLivenessPolicy.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <peripherals/DimmedPump.h>
#include <peripherals/SimplePump.h>

#include <utility>

GaggiMateController::GaggiMateController(String version) : _version(std::move(version)) {
    configs.push_back(GM_STANDARD_REV_1X);
    configs.push_back(GM_STANDARD_REV_2X);
    configs.push_back(GM_PRO_REV_1x);
    configs.push_back(GM_PRO_LEGO);
    configs.push_back(GM_PRO_REV_11);
}

bool GaggiMateController::isSteamSwitchOn() const {
    // Upstream v1.9 re-pairing escape hatch: steam switch held at power-on opens the
    // BLE pairing window. Active low; require a steady reading so a bouncing contact
    // never opens the window.
    pinMode(_config.steamButtonPin, INPUT_PULLUP);
    for (int i = 0; i < 5; i++) {
        if (digitalRead(_config.steamButtonPin) != LOW)
            return false;
        delay(10);
    }
    return true;
}

void GaggiMateController::setup() {
    delay(5000);
    detectBoard();
    detectAddon();

    this->thermocouple = new Max31855Thermocouple(
        _config.maxCsPin, _config.maxMisoPin, _config.maxSckPin, [this](float temperature) { /* noop */ },
        [this]() { thermalRunawayShutdown(); });
    this->heater = new Heater(
        this->thermocouple, _config.heaterPin, [this]() { thermalRunawayShutdown(); },
        [this](float Kp, float Ki, float Kd) { _comms.sendAutotuneResult(Kp, Ki, Kd, 0.0f); });
    this->valve = new SimpleRelay(_config.valvePin, _config.valveOn);
    this->alt = new SimpleRelay(_config.altPin, _config.altOn);
    if (_config.capabilites.pressure) {
        pressureSensor = new PressureSensor(_config.pressureSda, _config.pressureScl, [this](float pressure) { /* noop */ });
    }
    if (_config.capabilites.dimming) {
        pump = new DimmedPump(_config.pumpPin, _config.pumpSensePin, pressureSensor);
    } else {
        pump = new SimplePump(_config.pumpPin, _config.pumpOn, _config.capabilites.ssrPump ? 1000.0f : 5000.0f);
    }
    this->brewBtn = new DigitalInput(_config.brewButtonPin, [this](const bool state) { _comms.sendButtonState(0, state); });
    this->steamBtn = new DigitalInput(_config.steamButtonPin, [this](const bool state) { _comms.sendButtonState(1, state); });

    // 4-Pin peripheral port
    if (!Wire.begin(_config.sunriseSdaPin, _config.sunriseSclPin, 400000)) {
        ESP_LOGE(LOG_TAG, "Failed to initialize I2C bus");
    }
    this->ledController = new LedController(&Wire);
    this->distanceSensor =
        new DistanceSensor(&Wire, [this](int distance) { _comms.sendTofMeasurement(static_cast<uint32_t>(distance)); });
    if (this->ledController->isAvailable()) {
        _config.capabilites.ledControls = true;
        _config.capabilites.tof = true;
        _comms.onLedControl([this](uint8_t channel, uint8_t brightness) { ledController->setChannel(channel, brightness); });
    }

    // PRO-655: SystemInfo (incl. protocol_version) is pushed over the framed link on
    // connect. dual_boiler stays false (zero-init) until PRO-657 wires ControllerConfig.
    gm::DeviceCapabilities capabilities = gaggimate_Capabilities_init_zero;
    capabilities.dimming = _config.capabilites.dimming;
    capabilities.pressure = _config.capabilites.pressure;
    capabilities.tof = _config.capabilites.tof;
    capabilities.led_control = _config.capabilites.ledControls;
    // Read the steam switch before steamBtn->setup() below.
    // PRO-303 (preserved): heap headroom right before NimBLE init.
    ESP_LOGI(LOG_TAG, "Pre-BLE-init heap: free=%u largest_block=%u", static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
    _comms.init("GPBLS", _config.name.c_str(), _version, capabilities, isSteamSwitchOn());

    if (_config.capabilites.ledControls) {
        this->ledController->setup();
    }
    if (_config.capabilites.tof) {
        this->distanceSensor->setup();
    }

    this->thermocouple->setup();
    this->heater->setup();
    this->valve->setup();
    this->alt->setup();
    this->pump->setup();
    this->brewBtn->setup();
    this->steamBtn->setup();
    if (_config.capabilites.pressure) {
        pressureSensor->setup();
        _comms.onPressureScale([this](float scale) { this->pressureSensor->setScale(scale); });
    }
    // Set up thermal feedforward for main heater if pressure/dimming capability exists
    if (heater && _config.capabilites.dimming && _config.capabilites.pressure) {
        auto dimmedPump = static_cast<DimmedPump *>(pump);
        float *pumpFlowPtr = dimmedPump->getPumpFlowPtr();
        int *valveStatusPtr = dimmedPump->getValveStatusPtr();

        heater->setThermalFeedforward(pumpFlowPtr, 23.0f, valveStatusPtr);
        heater->setFeedforwardScale(0.0f);
    }
    // Initialize last ping time
    lastPingTime.store(static_cast<uint32_t>(millis()), std::memory_order_relaxed);

    // PRO-655: output control arrives as per-component, device-numbered messages
    // (Boiler/Pump/Relay), usually batched in one frame by the display. They map
    // 1:1 onto the old Simple/AdvancedOutput callbacks: every control message feeds
    // the link watchdog via handlePing() and is dropped while errorState is set.
    _comms.onBoilerControl([this](uint8_t index, BoilerControlMode mode, float setpoint) {
        if (index != 0) { // single boiler; reject unknown devices
            ESP_LOGW(LOG_TAG, "Ignoring boiler control for unsupported index %u", index);
            return;
        }
        handlePing();
        if (errorState != ERROR_CODE_NONE) {
            return;
        }
        if (mode == BoilerControlMode::Temperature) {
            this->heater->setSetpoint(setpoint);
        } else {
            ESP_LOGW(LOG_TAG, "Boiler pressure mode requested but unsupported");
        }
    });
    _comms.onPumpControl([this](uint8_t index, PumpControlMode mode, float power, float pressure, float flow) {
        if (index != 0) { // single pump; reject unknown devices
            ESP_LOGW(LOG_TAG, "Ignoring pump control for unsupported index %u", index);
            return;
        }
        handlePing();
        if (errorState != ERROR_CODE_NONE) {
            return;
        }
        if (mode == PumpControlMode::Power) {
            this->pump->setPower(power);
            return;
        }
        if (!_config.capabilites.dimming) {
            return;
        }
        auto dimmedPump = static_cast<DimmedPump *>(pump);
        if (mode == PumpControlMode::Pressure) {
            dimmedPump->setPressureTarget(pressure, flow);
        } else { // PumpControlMode::Flow
            dimmedPump->setFlowTarget(flow, pressure);
        }
    });
    // Binary outputs: index 0 = brew valve, index 1 = alt relay.
    _comms.onRelayControl([this](uint8_t index, bool open) {
        if (index == 1) {
            // Alt relay (A-P3-2): deliberately does NOT call handlePing() (does not feed
            // the link watchdog) and is NOT errorState-gated. That is unchanged legacy
            // behaviour from the old AltControl path; it stays safe because
            // handlePingTimeout() forces alt off on every loop() while timed out.
            this->alt->set(open);
            return;
        }
        if (index != 0) {
            ESP_LOGW(LOG_TAG, "Ignoring relay control for unsupported index %u", index);
            return;
        }
        handlePing();
        if (errorState != ERROR_CODE_NONE) {
            return;
        }
        this->valve->set(open);
        if (_config.capabilites.dimming) {
            static_cast<DimmedPump *>(pump)->setValveState(open);
        }
    });
    _comms.onPidSettings([this](float Kp, float Ki, float Kd, float Kf) {
        this->heater->setTunings(Kp, Ki, Kd);

        // Apply thermal feedforward parameters if available
        this->heater->setFeedforwardScale(Kf);
    });
    _comms.onPumpSettings([this](gm::PumpSettings settings) {
        if (_config.capabilites.dimming) {
            auto dimmedPump = static_cast<DimmedPump *>(pump);
            // Check if this is a flow measurement call (a and b are flow measurements, c and d are nan)
            if (isnan(settings.c) && isnan(settings.d)) {
                dimmedPump->setPumpFlowCoeff(settings.a, settings.b); // a = oneBarFlow, b = nineBarFlow
            } else {
                dimmedPump->setPumpFlowPolyCoeffs(settings.a, settings.b, settings.c, settings.d);
            }
        }
    });
    _comms.onPing([this]() { handlePing(); });
    // Carlos's Heater::autotune(goal, windowSize); heaterWattage is not used by this Heater.
    // Like upstream v1.9: never (re-)engage the heater for autotune while faulted.
    _comms.onAutotune([this](uint32_t testTime, uint32_t samples, uint32_t /*heaterWattage*/) {
        handlePing();
        if (errorState != ERROR_CODE_NONE) {
            return;
        }
        this->heater->autotune(static_cast<int>(testTime), static_cast<int>(samples));
    });
    _comms.onTare([this]() {
        if (!_config.capabilites.dimming) {
            return;
        }
        auto dimmedPump = static_cast<DimmedPump *>(pump);
        dimmedPump->tare();
    });
    // PRO-655 R2: the only writable comms characteristic is the framed RX char
    // (BleServerTransport::onWrite ignores every other characteristic), so a legacy
    // NimBLEComm display's per-message writes (output/ping/pid/...) can never reach
    // these handlers; they have no characteristic to land on.
    ESP_LOGI(LOG_TAG, "Initialization done");
}

void GaggiMateController::loop() {
    const uint32_t now = static_cast<uint32_t>(millis());
    if (link_liveness::pingTimedOut(now, lastPingTime.load(std::memory_order_relaxed), PING_TIMEOUT_MS)) {
        handlePingTimeout();
    }
    sendSensorData();
    delay(250);
}

void GaggiMateController::registerBoardConfig(ControllerConfig config) { configs.push_back(config); }

void GaggiMateController::detectBoard() {
    constexpr int MAX_DETECT_RETRIES = 3;
    pinMode(DETECT_EN_PIN, OUTPUT);
    pinMode(DETECT_VALUE_PIN, INPUT_PULLDOWN);

    for (int attempt = 0; attempt < MAX_DETECT_RETRIES; attempt++) {
        digitalWrite(DETECT_EN_PIN, HIGH);
        delay(10); // Allow voltage to stabilize before ADC read
        uint16_t millivolts = analogReadMilliVolts(DETECT_VALUE_PIN);
        digitalWrite(DETECT_EN_PIN, LOW);
        int boardId = round(((float)millivolts) / 100.0f - 0.5f);
        ESP_LOGI(LOG_TAG, "Board detect attempt %d/%d: ID=%d (raw: %d mV)", attempt + 1, MAX_DETECT_RETRIES, boardId, millivolts);
        for (ControllerConfig config : configs) {
            if (config.autodetectValue == boardId) {
                _config = config;
                ESP_LOGI(LOG_TAG, "Using Board: %s", _config.name.c_str());
                return;
            }
        }
        ESP_LOGW(LOG_TAG, "No match on attempt %d, retrying...", attempt + 1);
        delay(500);
    }
    ESP_LOGE(LOG_TAG, "No compatible board detected after %d attempts. Restarting...", MAX_DETECT_RETRIES);
    delay(5000);
    ESP.restart();
}

void GaggiMateController::detectAddon() {
    // TODO: Add I2C scanning for extensions
}

void GaggiMateController::handlePing() {
    if (errorState == ERROR_CODE_TIMEOUT) {
        errorState = ERROR_CODE_NONE;
    }
    lastPingTime.store(static_cast<uint32_t>(millis()), std::memory_order_relaxed);
    ESP_LOGV(LOG_TAG, "Ping received, system is alive");
}

void GaggiMateController::handlePingTimeout() {
    // PRO-655 R1: outputs are forced off on EVERY call while timed out (loop() re-enters
    // every 250 ms), exactly as before; only the log + link drop are edge-triggered.
    this->heater->setSetpoint(0);
    this->pump->setPower(0);
    this->valve->set(false);
    this->alt->set(false);
    const bool alreadyTimedOut = errorState == ERROR_CODE_TIMEOUT;
    if (!alreadyTimedOut) {
        ESP_LOGE(LOG_TAG, "Ping timeout detected. Turning off heater and pump for safety.");
    }
    // Upstream v1.9: drop a possibly wedged GATT link so the display rebuilds it and
    // re-sends control state (not during controller OTA).
    if (link_liveness::shouldDropLinkOnTimeout(alreadyTimedOut, _comms.isUpdating())) {
        _comms.disconnect();
    }
    errorState = ERROR_CODE_TIMEOUT;
}

void GaggiMateController::thermalRunawayShutdown() {
    ESP_LOGE(LOG_TAG, "Thermal runaway detected! Turning off heater and pump!\n");
    // Turn off the heater and pump immediately
    this->heater->setSetpoint(0);
    this->pump->setPower(0);
    this->valve->set(false);
    this->alt->set(false);
    errorState = ERROR_CODE_RUNAWAY;
    _comms.sendError(ERROR_CODE_RUNAWAY);
}

void GaggiMateController::sendSensorData() {
    if (_config.capabilites.pressure) {
        auto dimmedPump = static_cast<DimmedPump *>(pump);
        // Sensor + (optional) volumetric ride in one frame; telemetry is fire-and-forget.
        gm::Payload batch[2];
        size_t n = 0;
        batch[n++] =
            _comms.buildSensorData(this->thermocouple->read(), this->pressureSensor->getPressure(), dimmedPump->getPuckFlow(),
                                   dimmedPump->getPumpFlow(), dimmedPump->getPuckResistance());
        if (this->valve->getState()) {
            batch[n++] = _comms.buildVolumetricMeasurement(dimmedPump->getCoffeeVolume());
        }
        _comms.sendUnreliableBatch(batch, n);
    } else {
        _comms.sendSensorData(this->thermocouple->read(), 0.0f, 0.0f, 0.0f, 0.0f);
    }
}
