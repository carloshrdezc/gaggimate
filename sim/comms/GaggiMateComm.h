// Plain protocol vocabulary used by the simulator's MockController and the
// NimBLEClientController mock. Upstream re-includes this from lib/NanoPbComm;
// this fork doesn't have that lib (it uses lib/NimBLEComm), so the simulator
// carries a self-contained copy of the plain command types here — they have no
// Arduino/NimBLE dependencies and never touch the on-wire encoding. [CAR-399]
#pragma once

#include <cstdint>

// Pump control mode. Integer values match gaggimate_PumpMode in the schema.
enum class PumpControlMode : uint8_t {
    Power = 0,    // drive at a fixed power percentage
    Pressure = 1, // target a pressure, flow is the limit
    Flow = 2,     // target a flow, pressure is the limit
};

// Boiler control mode. Integer values match gaggimate_BoilerMode in the schema.
enum class BoilerControlMode : uint8_t {
    Temperature = 0, // setpoint is a target temperature in degC
    Pressure = 1,    // setpoint is a target pressure in bar
};

// Per-component commands the mock controller reacts to.
struct BoilerCommand {
    uint8_t index = 0;
    BoilerControlMode mode = BoilerControlMode::Temperature;
    float setpoint = 0.0f;
    bool operator==(const BoilerCommand &o) const { return index == o.index && mode == o.mode && setpoint == o.setpoint; }
    bool operator!=(const BoilerCommand &o) const { return !(*this == o); }
};
struct PumpCommand {
    uint8_t index = 0;
    PumpControlMode mode = PumpControlMode::Power;
    float power = 0.0f;
    float pressure = 0.0f;
    float flow = 0.0f;
    bool operator==(const PumpCommand &o) const {
        return index == o.index && mode == o.mode && power == o.power && pressure == o.pressure && flow == o.flow;
    }
    bool operator!=(const PumpCommand &o) const { return !(*this == o); }
};
struct RelayCommand {
    uint8_t index = 0;
    bool open = false;
    bool operator==(const RelayCommand &o) const { return index == o.index && open == o.open; }
    bool operator!=(const RelayCommand &o) const { return !(*this == o); }
};
struct LedChannelCommand {
    uint8_t channel = 0;
    uint8_t brightness = 0;
    bool operator==(const LedChannelCommand &o) const { return channel == o.channel && brightness == o.brightness; }
    bool operator!=(const LedChannelCommand &o) const { return !(*this == o); }
};

// Error codes (match the device protocol so firmware comparisons keep working).
constexpr int ERROR_CODE_NONE = 0;
constexpr int ERROR_CODE_COMM_SEND = 1;
constexpr int ERROR_CODE_COMM_RCV = 2;
constexpr int ERROR_CODE_PROTO_ERR = 3;
constexpr int ERROR_CODE_RUNAWAY = 4;
constexpr int ERROR_CODE_TIMEOUT = 5;
constexpr int ERROR_CODE_AUTOTUNE_TIMEOUT = 6;
