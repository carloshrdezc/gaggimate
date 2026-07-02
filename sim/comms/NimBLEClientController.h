// Simulator stand-in for lib/NimBLEComm/src/NimBLEClientController.h. Presents
// the exact public API the firmware's Controller calls, but instead of talking
// to a real ESP32 controller over BLE it drives a local MockController thermal/
// hydraulic model and fires the registered callbacks directly. The simulator
// build excludes the real (NimBLE-dependent) .cpp and compiles
// sim/comms/NimBLEClientController.cpp instead. [CAR-399]
#ifndef NIMBLECLIENTCONTROLLER_H
#define NIMBLECLIENTCONTROLLER_H

#include "MockController.h"
#include "NimBLEClient.h"
#include "NimBLEComm.h"
#include <Arduino.h>
#include <cstdint>
#include <string>

class NimBLEClientController {
  public:
    NimBLEClientController();

    void initClient();
    bool connectToServer();
    void loop();

    void sendAdvancedOutputControl(bool valve, float boilerSetpoint, bool pressureTarget, float pressure, float flow);
    void sendOutputControl(bool valve, float pumpSetpoint, float boilerSetpoint);
    void sendAltControl(bool pinState);
    void sendPing();
    void sendAutotune(int testTime, int samples);
    void sendPidSettings(const String &pid);
    void sendPumpModelCoeffs(const String &pumpModelCoeffs);
    void setPressureScale(float scale);
    void sendLedControl(uint8_t channel, uint8_t brightness);
    bool isReadyForConnection() const;
    bool isConnected();
    void scan();
    void tare();
    void registerRemoteErrorCallback(const remote_err_callback_t &callback);
    void registerBrewBtnCallback(const brew_callback_t &callback);
    void registerSteamBtnCallback(const steam_callback_t &callback);
    void registerSensorCallback(const sensor_read_callback_t &callback);
    void registerAutotuneResultCallback(const pid_control_callback_t &callback);
    void registerVolumetricMeasurementCallback(const float_callback_t &callback);
    void registerTofMeasurementCallback(const int_callback_t &callback);
    void registerDisconnectCallback(const void_callback_t &callback);
    std::string readInfo() const;
    NimBLEClient *getClient() const { return const_cast<NimBLEClient *>(&_client); }

  private:
    MockController _mock;
    NimBLEClient _client;

    bool _initialized = false;
    bool _connected = false;

    // Autotune is faked: after a short delay we report plausible PID gains.
    bool _autotunePending = false;
    uint32_t _autotuneDueMs = 0;

    remote_err_callback_t _remoteErrorCallback = nullptr;
    brew_callback_t _brewBtnCallback = nullptr;
    steam_callback_t _steamBtnCallback = nullptr;
    pid_control_callback_t _autotuneResultCallback = nullptr;
    sensor_read_callback_t _sensorCallback = nullptr;
    float_callback_t _volumetricMeasurementCallback = nullptr;
    int_callback_t _tofMeasurementCallback = nullptr;
    void_callback_t _disconnectCallback = nullptr;
};

#endif // NIMBLECLIENTCONTROLLER_H
