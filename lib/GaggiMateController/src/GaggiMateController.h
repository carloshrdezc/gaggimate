#ifndef GAGGIMATECONTROLLER_H
#define GAGGIMATECONTROLLER_H
#include "ControllerConfig.h"
#include "GaggiMateServer.h"
#include <peripherals/DigitalInput.h>
#include <peripherals/DistanceSensor.h>
#include <peripherals/Heater.h>
#include <peripherals/LedController.h>
#include <peripherals/Max31855Thermocouple.h>
#include <peripherals/PressureSensor.h>
#include <peripherals/Pump.h>
#include <peripherals/SimpleRelay.h>
#include <atomic>
#include <cstdint>
#include <vector>

constexpr double PING_TIMEOUT_SECONDS = 20.0;
// Integer-ms form used by link_liveness::pingTimedOut (strict >). 20999 keeps the
// legacy integer-seconds trip point: (elapsed / 1000) > 20 <=> elapsed >= 21000.
constexpr uint32_t PING_TIMEOUT_MS = 20999;

constexpr int DETECT_EN_PIN = 40;
constexpr int DETECT_VALUE_PIN = 11;

class GaggiMateController {
  public:
    GaggiMateController(String version);
    void setup(void);
    void loop(void);

    void registerBoardConfig(ControllerConfig config);

  private:
    void detectBoard();
    void detectAddon();
    bool isSteamSwitchOn() const;
    void handlePing();
    void handlePingTimeout(void);
    void thermalRunawayShutdown(void);
    void startPidAutotune(void);
    void stopPidAutotune(void);
    void sendSensorData(void);

    ControllerConfig _config = ControllerConfig{};
    GaggiMateServer _comms;

    Max31855Thermocouple *thermocouple = nullptr;
    Heater *heater = nullptr;
    SimpleRelay *valve = nullptr;
    SimpleRelay *alt = nullptr;
    Pump *pump = nullptr;
    DigitalInput *brewBtn = nullptr;
    DigitalInput *steamBtn = nullptr;
    PressureSensor *pressureSensor = nullptr;
    LedController *ledController = nullptr;
    DistanceSensor *distanceSensor = nullptr;

    std::vector<ControllerConfig> configs;

    String _version;
    // A-P3-1: written by the NimBLE host task (handlePing), read by loop().
    std::atomic<uint32_t> lastPingTime{0};
    int errorState = ERROR_CODE_NONE;

    const char *LOG_TAG = "GaggiMateController";
};

#endif // GAGGIMATECONTROLLER_H
