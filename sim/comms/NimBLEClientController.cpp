// Simulator implementation of the firmware's NimBLEClientController. Drives a
// local MockController instead of a BLE link and fires the firmware's registered
// callbacks directly. See sim/comms/NimBLEClientController.h. [CAR-399]
#include "NimBLEClientController.h"

#include <Arduino.h>
#include <cstdint>

NimBLEClientController::NimBLEClientController() = default;

void NimBLEClientController::initClient() { _initialized = true; }

bool NimBLEClientController::isReadyForConnection() const { return _initialized; }

bool NimBLEClientController::isConnected() { return _connected; }

bool NimBLEClientController::connectToServer() {
    _connected = true;
    // Bridge the mock model's telemetry to the firmware's sensor callbacks.
    _mock.onSensor = [this](float temp, float pressure, float puckFlow, float pumpFlow, float puckResistance) {
        if (_sensorCallback)
            _sensorCallback(temp, pressure, puckFlow, pumpFlow, puckResistance);
    };
    _mock.onVolumetric = [this](float volume) {
        if (_volumetricMeasurementCallback)
            _volumetricMeasurementCallback(volume);
    };
    _mock.onTof = [this](uint32_t distance) {
        if (_tofMeasurementCallback)
            _tofMeasurementCallback(static_cast<int>(distance));
    };
    _mock.begin();
    return true;
}

void NimBLEClientController::loop() {
    if (!_connected)
        return;
    _mock.update();

    // Fake autotune: report plausible PID gains a short time after a request.
    if (_autotunePending && millis() >= _autotuneDueMs) {
        _autotunePending = false;
        if (_autotuneResultCallback)
            _autotuneResultCallback(2.4f, 0.08f, 18.0f, 0.0f);
    }
}

// Simple output: pump drive as a 0..100 power percentage, boiler as a target
// temperature, relay = brew valve.
void NimBLEClientController::sendOutputControl(bool valve, float pumpSetpoint, float boilerSetpoint) {
    _mock.setBoiler(BoilerCommand{0, BoilerControlMode::Temperature, boilerSetpoint});
    _mock.setPump(PumpCommand{0, PumpControlMode::Power, pumpSetpoint, 0.0f, 0.0f});
    _mock.setRelay(RelayCommand{0, valve});
}

// Advanced output: pressure- or flow-targeted pump, boiler temperature, relay.
void NimBLEClientController::sendAdvancedOutputControl(bool valve, float boilerSetpoint, bool pressureTarget, float pressure,
                                                       float flow) {
    _mock.setBoiler(BoilerCommand{0, BoilerControlMode::Temperature, boilerSetpoint});
    if (pressureTarget)
        _mock.setPump(PumpCommand{0, PumpControlMode::Pressure, 0.0f, pressure, flow});
    else
        _mock.setPump(PumpCommand{0, PumpControlMode::Flow, 0.0f, pressure, flow});
    _mock.setRelay(RelayCommand{0, valve});
}

void NimBLEClientController::sendAltControl(bool /*pinState*/) {}
void NimBLEClientController::sendPing() {}

void NimBLEClientController::sendAutotune(int testTime, int /*samples*/) {
    _autotunePending = true;
    // Report a result a couple of seconds in, regardless of the requested window.
    const uint32_t delayMs = testTime > 0 ? static_cast<uint32_t>(testTime) * 100u : 2000u;
    _autotuneDueMs = millis() + delayMs;
}

void NimBLEClientController::sendPidSettings(const String & /*pid*/) {}
void NimBLEClientController::sendPumpModelCoeffs(const String & /*pumpModelCoeffs*/) {}
void NimBLEClientController::setPressureScale(float /*scale*/) {}
void NimBLEClientController::sendLedControl(uint8_t /*channel*/, uint8_t /*brightness*/) {}
void NimBLEClientController::scan() {}
void NimBLEClientController::tare() { _mock.tareScale(); }

void NimBLEClientController::registerRemoteErrorCallback(const remote_err_callback_t &callback) {
    _remoteErrorCallback = callback;
}
void NimBLEClientController::registerBrewBtnCallback(const brew_callback_t &callback) { _brewBtnCallback = callback; }
void NimBLEClientController::registerSteamBtnCallback(const steam_callback_t &callback) { _steamBtnCallback = callback; }
void NimBLEClientController::registerSensorCallback(const sensor_read_callback_t &callback) { _sensorCallback = callback; }
void NimBLEClientController::registerAutotuneResultCallback(const pid_control_callback_t &callback) {
    _autotuneResultCallback = callback;
}
void NimBLEClientController::registerVolumetricMeasurementCallback(const float_callback_t &callback) {
    _volumetricMeasurementCallback = callback;
}
void NimBLEClientController::registerTofMeasurementCallback(const int_callback_t &callback) {
    _tofMeasurementCallback = callback;
}
void NimBLEClientController::registerDisconnectCallback(const void_callback_t &callback) { _disconnectCallback = callback; }

// Report a pressure-capable "pro" machine with LED + ToF so the full UI is
// exercised in the simulator.
std::string NimBLEClientController::readInfo() const {
    return "{\"hw\":\"GaggiMate Pro (Simulator)\",\"v\":\"sim\",\"cp\":{\"dm\":true,\"ps\":true,\"led\":true,\"tof\":true}}";
}
