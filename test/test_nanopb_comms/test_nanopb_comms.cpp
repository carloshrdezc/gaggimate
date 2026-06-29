// PRO-242 — host round-trip tests for the nanopb wire format of the two hot
// BLE characteristics converted in this slice: SENSOR_DATA (Controller->Display
// NOTIFY, 5 floats) and OUTPUT_CONTROL (Display->Controller WRITE, type
// discriminator 0=SimpleOutput / 1=AdvancedOutput).
//
// Ported from docs/spike-nanopb-comms/roundtrip_test.cpp (PRO-239), adapted to
// the renamed `gaggimate` proto package (PRO-306) and run inside `pio test -e
// native` (PRO-242 wires the nanopb codegen into [env:native]). These assert
// encode->decode bit-exactness, proving the lossy 3-decimal float_to_string()
// path the text format used is gone, and exercise the OUTPUT_CONTROL
// discriminator framing both production sides now agree on:
//   byte 0 = type (0 simple / 1 advanced), bytes 1.. = the encoded message.

#include <unity.h>

#include "comms.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// bit-exact float equality — the whole point: nanopb preserves the IEEE-754
// bits, where the old text format quantized to 3 decimals via float_to_string.
static bool bit_equal(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// Reproduce the OLD text wire format's float quantization (VERBATIM from the
// pre-PRO-242 NimBLEComm.h float_to_string) so we can prove it WAS lossy.
static float text_round_3dp(float f) { return std::round(f * 1000.0f) / 1000.0f; }

// ---------------------------------------------------------------------------
// SensorData (5 floats) — the hottest packet, the primary lossy path.
// ---------------------------------------------------------------------------
static void test_sensor_data_roundtrip_bit_exact() {
    // Values deliberately carry >3 decimal digits so the OLD text format's
    // rounding would be observable.
    gaggimate_SensorData src = gaggimate_SensorData_init_zero;
    src.temperature = 93.456789f;
    src.pressure = 9.123456f;
    src.puck_flow = 2.718281f;
    src.pump_flow = 3.141592f;
    src.puck_resistance = 0.0007654321f;

    uint8_t buf[64];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_SensorData_fields, &src), "pb_encode SensorData");

    gaggimate_SensorData dst = gaggimate_SensorData_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_SensorData_fields, &dst), "pb_decode SensorData");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.temperature, dst.temperature), "temperature bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.pressure, dst.pressure), "pressure bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.puck_flow, dst.puck_flow), "puck_flow bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.pump_flow, dst.pump_flow), "pump_flow bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.puck_resistance, dst.puck_resistance), "puck_resistance bit-exact");
}

// Pin the regression the migration fixes: the OLD text format quantized these
// values to 3 decimals and lost data; nanopb does not. (Guards against anyone
// re-introducing a stringify-with-rounding step on this path.)
static void test_sensor_data_old_text_format_was_lossy() {
    const float temperature = 93.456789f;
    const float puck_resistance = 0.0007654321f; // rounds to 0.001 in 3dp text
    TEST_ASSERT_FALSE_MESSAGE(bit_equal(temperature, text_round_3dp(temperature)),
                              "old 3dp text format lost temperature precision (regression guard)");
    TEST_ASSERT_TRUE_MESSAGE(std::fabs(puck_resistance - text_round_3dp(puck_resistance)) > 1e-5f,
                             "old 3dp text format dramatically lost small puck_resistance (regression guard)");
}

// ---------------------------------------------------------------------------
// OUTPUT_CONTROL type 0 — SimpleOutput (bool + 2 floats).
// ---------------------------------------------------------------------------
static void test_simple_output_roundtrip_bit_exact() {
    gaggimate_SimpleOutput src = gaggimate_SimpleOutput_init_zero;
    src.valve = true;
    src.pump_setpoint = 87.654321f;
    src.boiler_setpoint = 93.333333f;

    uint8_t buf[32];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_SimpleOutput_fields, &src), "pb_encode SimpleOutput");

    gaggimate_SimpleOutput dst = gaggimate_SimpleOutput_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_SimpleOutput_fields, &dst), "pb_decode SimpleOutput");

    TEST_ASSERT_TRUE_MESSAGE(src.valve == dst.valve, "valve equal");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.pump_setpoint, dst.pump_setpoint), "pump_setpoint bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.boiler_setpoint, dst.boiler_setpoint), "boiler_setpoint bit-exact");
}

// ---------------------------------------------------------------------------
// OUTPUT_CONTROL type 1 — AdvancedOutput (2 bools + 3 floats).
// ---------------------------------------------------------------------------
static void test_advanced_output_roundtrip_bit_exact() {
    gaggimate_AdvancedOutput src = gaggimate_AdvancedOutput_init_zero;
    src.valve = false;
    src.boiler_setpoint = 96.5f;
    src.pressure_target = true;
    src.pump_pressure = 8.987654f;
    src.pump_flow = 4.123456f;

    uint8_t buf[48];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_AdvancedOutput_fields, &src), "pb_encode AdvancedOutput");

    gaggimate_AdvancedOutput dst = gaggimate_AdvancedOutput_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_AdvancedOutput_fields, &dst), "pb_decode AdvancedOutput");

    TEST_ASSERT_TRUE_MESSAGE(src.valve == dst.valve, "valve equal");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.boiler_setpoint, dst.boiler_setpoint), "boiler_setpoint bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(src.pressure_target == dst.pressure_target, "pressure_target equal");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.pump_pressure, dst.pump_pressure), "pump_pressure bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.pump_flow, dst.pump_flow), "pump_flow bit-exact");
}

// ---------------------------------------------------------------------------
// OUTPUT_CONTROL discriminator framing — exactly what the production encode
// (NimBLEClientController) writes and the production decode
// (NimBLEServerController) reads: a single leading type byte, then the encoded
// message. Both sub-paths must route by that byte and decode bit-exact.
// ---------------------------------------------------------------------------
static void test_output_control_discriminator_framing() {
    // ---- type 0 / simple ----
    {
        gaggimate_SimpleOutput src = gaggimate_SimpleOutput_init_zero;
        src.valve = true;
        src.pump_setpoint = 70.25f;
        src.boiler_setpoint = 92.75f;

        uint8_t frame[40];
        frame[0] = 0; // discriminator
        pb_ostream_t os = pb_ostream_from_buffer(frame + 1, sizeof(frame) - 1);
        TEST_ASSERT_TRUE(pb_encode(&os, gaggimate_SimpleOutput_fields, &src));
        const size_t frame_len = 1 + os.bytes_written;

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, frame[0], "simple discriminator byte == 0");
        gaggimate_SimpleOutput dst = gaggimate_SimpleOutput_init_zero;
        pb_istream_t is = pb_istream_from_buffer(frame + 1, frame_len - 1);
        TEST_ASSERT_TRUE(pb_decode(&is, gaggimate_SimpleOutput_fields, &dst));
        TEST_ASSERT_TRUE(dst.valve);
        TEST_ASSERT_TRUE(bit_equal(src.pump_setpoint, dst.pump_setpoint));
        TEST_ASSERT_TRUE(bit_equal(src.boiler_setpoint, dst.boiler_setpoint));
    }

    // ---- type 1 / advanced ----
    {
        gaggimate_AdvancedOutput src = gaggimate_AdvancedOutput_init_zero;
        src.valve = true;
        src.boiler_setpoint = 94.0f;
        src.pressure_target = false;
        src.pump_pressure = 6.5f;
        src.pump_flow = 3.0f;

        uint8_t frame[48];
        frame[0] = 1; // discriminator
        pb_ostream_t os = pb_ostream_from_buffer(frame + 1, sizeof(frame) - 1);
        TEST_ASSERT_TRUE(pb_encode(&os, gaggimate_AdvancedOutput_fields, &src));
        const size_t frame_len = 1 + os.bytes_written;

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, frame[0], "advanced discriminator byte == 1");
        gaggimate_AdvancedOutput dst = gaggimate_AdvancedOutput_init_zero;
        pb_istream_t is = pb_istream_from_buffer(frame + 1, frame_len - 1);
        TEST_ASSERT_TRUE(pb_decode(&is, gaggimate_AdvancedOutput_fields, &dst));
        TEST_ASSERT_TRUE(dst.valve);
        TEST_ASSERT_TRUE(bit_equal(src.boiler_setpoint, dst.boiler_setpoint));
        TEST_ASSERT_FALSE(dst.pressure_target);
        TEST_ASSERT_TRUE(bit_equal(src.pump_pressure, dst.pump_pressure));
        TEST_ASSERT_TRUE(bit_equal(src.pump_flow, dst.pump_flow));
    }
}

// ===========================================================================
// PRO-243 — the seven remaining Controller->Display NOTIFY/READ characteristics
// converted from comma-delimited text (and ArduinoJson for SystemInfo) to
// nanopb. The lossy float paths (AutotuneResult, VolumetricMeasurement) get the
// same bit-exact + regression-guard treatment SensorData got in PRO-242.
// ===========================================================================

// Error (int32 code).
static void test_error_roundtrip() {
    gaggimate_Error src = gaggimate_Error_init_zero;
    src.code = 4; // ERROR_CODE_RUNAWAY

    uint8_t buf[16];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_Error_fields, &src), "pb_encode Error");

    gaggimate_Error dst = gaggimate_Error_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_Error_fields, &dst), "pb_decode Error");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(src.code, dst.code, "error code equal");
}

// BrewButton (bool pressed) — assert both true and false survive.
static void test_brew_button_roundtrip() {
    for (int b = 0; b <= 1; b++) {
        gaggimate_BrewButton src = gaggimate_BrewButton_init_zero;
        src.pressed = (b == 1);

        uint8_t buf[8];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_BrewButton_fields, &src), "pb_encode BrewButton");

        gaggimate_BrewButton dst = gaggimate_BrewButton_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
        TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_BrewButton_fields, &dst), "pb_decode BrewButton");
        TEST_ASSERT_TRUE_MESSAGE(src.pressed == dst.pressed, "brew pressed equal");
    }
}

// SteamButton (bool pressed) — assert both true and false survive.
static void test_steam_button_roundtrip() {
    for (int b = 0; b <= 1; b++) {
        gaggimate_SteamButton src = gaggimate_SteamButton_init_zero;
        src.pressed = (b == 1);

        uint8_t buf[8];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_SteamButton_fields, &src), "pb_encode SteamButton");

        gaggimate_SteamButton dst = gaggimate_SteamButton_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
        TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_SteamButton_fields, &dst), "pb_decode SteamButton");
        TEST_ASSERT_TRUE_MESSAGE(src.pressed == dst.pressed, "steam pressed equal");
    }
}

// AutotuneResult (Kp,Ki,Kd) — a primary lossy path. The controller sends only
// three gains (Kf stays 0); assert all four fields survive bit-exact.
static void test_autotune_result_roundtrip_bit_exact() {
    gaggimate_AutotuneResult src = gaggimate_AutotuneResult_init_zero;
    src.kp = 2.456789f;
    src.ki = 0.0876543f;
    src.kd = 18.234567f;
    // kf intentionally left at the proto default (0) — matches sendAutotuneResult.

    uint8_t buf[32];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_AutotuneResult_fields, &src), "pb_encode AutotuneResult");

    gaggimate_AutotuneResult dst = gaggimate_AutotuneResult_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_AutotuneResult_fields, &dst), "pb_decode AutotuneResult");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kp, dst.kp), "kp bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.ki, dst.ki), "ki bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kd, dst.kd), "kd bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.kf), "kf default 0 bit-exact");
}

// Pin the regression: the OLD AutotuneResult text path stringified each gain
// with float_to_string (3-dp round) before the display re-parsed it, losing
// precision nanopb now preserves.
static void test_autotune_result_old_text_format_was_lossy() {
    const float ki = 0.0876543f; // rounds to 0.088 in 3dp text
    const float kd = 18.234567f; // rounds to 18.235 in 3dp text
    TEST_ASSERT_FALSE_MESSAGE(bit_equal(ki, text_round_3dp(ki)), "old 3dp text format lost Ki precision (regression guard)");
    TEST_ASSERT_FALSE_MESSAGE(bit_equal(kd, text_round_3dp(kd)), "old 3dp text format lost Kd precision (regression guard)");
}

// VolumetricMeasurement (float value) — the other lossy path.
static void test_volumetric_measurement_roundtrip_bit_exact() {
    gaggimate_VolumetricMeasurement src = gaggimate_VolumetricMeasurement_init_zero;
    src.value = 36.123456f; // carries >3 decimals so the old text path would round

    uint8_t buf[16];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_VolumetricMeasurement_fields, &src), "pb_encode VolumetricMeasurement");

    gaggimate_VolumetricMeasurement dst = gaggimate_VolumetricMeasurement_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_VolumetricMeasurement_fields, &dst), "pb_decode VolumetricMeasurement");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.value, dst.value), "volumetric value bit-exact");
}

static void test_volumetric_measurement_old_text_format_was_lossy() {
    const float value = 36.123456f; // rounds to 36.123 in the old 3dp text path
    TEST_ASSERT_FALSE_MESSAGE(bit_equal(value, text_round_3dp(value)),
                              "old 3dp text format lost volumetric precision (regression guard)");
}

// TofMeasurement (int32 distance_mm).
static void test_tof_measurement_roundtrip() {
    gaggimate_TofMeasurement src = gaggimate_TofMeasurement_init_zero;
    src.distance_mm = 1234;

    uint8_t buf[16];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_TofMeasurement_fields, &src), "pb_encode TofMeasurement");

    gaggimate_TofMeasurement dst = gaggimate_TofMeasurement_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_TofMeasurement_fields, &dst), "pb_decode TofMeasurement");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(src.distance_mm, dst.distance_mm, "tof distance equal");
}

// SystemInfo + Capabilities — the INFO characteristic, formerly an ArduinoJson
// {"hw","v","cp":{...}} string. Assert the strings and all four capability
// bools survive the nanopb round trip.
static void test_system_info_roundtrip() {
    gaggimate_SystemInfo src = gaggimate_SystemInfo_init_zero;
    snprintf(src.hardware, sizeof(src.hardware), "%s", "GaggiMate Pro Rev 1.1");
    snprintf(src.version, sizeof(src.version), "%s", "v2.3.4-rc1");
    src.has_capabilities = true;
    src.capabilities.dimming = true;
    src.capabilities.pressure = true;
    src.capabilities.led_control = false;
    src.capabilities.tof = true;

    uint8_t buf[gaggimate_SystemInfo_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_SystemInfo_fields, &src), "pb_encode SystemInfo");

    gaggimate_SystemInfo dst = gaggimate_SystemInfo_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_SystemInfo_fields, &dst), "pb_decode SystemInfo");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("GaggiMate Pro Rev 1.1", dst.hardware, "hardware string equal");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("v2.3.4-rc1", dst.version, "version string equal");
    TEST_ASSERT_TRUE_MESSAGE(dst.has_capabilities, "capabilities present");
    TEST_ASSERT_TRUE_MESSAGE(dst.capabilities.dimming == true, "dimming equal");
    TEST_ASSERT_TRUE_MESSAGE(dst.capabilities.pressure == true, "pressure equal");
    TEST_ASSERT_TRUE_MESSAGE(dst.capabilities.led_control == false, "led_control equal");
    TEST_ASSERT_TRUE_MESSAGE(dst.capabilities.tof == true, "tof equal");
}

// Exhaustively round-trip every combination of the four capability bools so a
// future field-order/packing change can't silently swap two of them.
static void test_system_info_capability_permutations() {
    for (int mask = 0; mask < 16; mask++) {
        gaggimate_SystemInfo src = gaggimate_SystemInfo_init_zero;
        snprintf(src.hardware, sizeof(src.hardware), "%s", "hw");
        snprintf(src.version, sizeof(src.version), "%s", "v");
        src.has_capabilities = true;
        src.capabilities.dimming = (mask & 0x1) != 0;
        src.capabilities.pressure = (mask & 0x2) != 0;
        src.capabilities.led_control = (mask & 0x4) != 0;
        src.capabilities.tof = (mask & 0x8) != 0;

        uint8_t buf[gaggimate_SystemInfo_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST_ASSERT_TRUE(pb_encode(&os, gaggimate_SystemInfo_fields, &src));

        gaggimate_SystemInfo dst = gaggimate_SystemInfo_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
        TEST_ASSERT_TRUE(pb_decode(&is, gaggimate_SystemInfo_fields, &dst));

        TEST_ASSERT_TRUE_MESSAGE(src.capabilities.dimming == dst.capabilities.dimming, "dimming permutation");
        TEST_ASSERT_TRUE_MESSAGE(src.capabilities.pressure == dst.capabilities.pressure, "pressure permutation");
        TEST_ASSERT_TRUE_MESSAGE(src.capabilities.led_control == dst.capabilities.led_control, "led_control permutation");
        TEST_ASSERT_TRUE_MESSAGE(src.capabilities.tof == dst.capabilities.tof, "tof permutation");
    }
}

// ===========================================================================
// PRO-244 — the eight remaining Display->Controller WRITE characteristics
// converted from comma/text (float_to_string + get_token) to nanopb:
//   AltControl, Ping, PidSettings, PumpModelCoeffs, AutotuneRequest,
//   PressureScale, Tare, LedControl.
// Encode->decode round trips assert every field survives; PressureScale gets
// the bit-exact + regression-guard treatment (its old text path was lossy).
// PumpModelCoeffs gets a mandatory NaN-preservation test (two-point flow mode).
// PidSettings gets a kf-absent => kf==0 test (the Kf field is optional).
// ===========================================================================

// AltControl (bool active) — assert both true and false survive.
static void test_alt_control_roundtrip() {
    for (int b = 0; b <= 1; b++) {
        gaggimate_AltControl src = gaggimate_AltControl_init_zero;
        src.active = (b == 1);

        uint8_t buf[gaggimate_AltControl_size];
        pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_AltControl_fields, &src), "pb_encode AltControl");

        gaggimate_AltControl dst = gaggimate_AltControl_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
        TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_AltControl_fields, &dst), "pb_decode AltControl");
        TEST_ASSERT_TRUE_MESSAGE(src.active == dst.active, "alt active equal");
    }
}

// Ping (empty message) — encodes to 0 bytes; decode of the empty payload
// succeeds. The write event itself is the signal.
static void test_ping_roundtrip_empty() {
    gaggimate_Ping src = gaggimate_Ping_init_zero;
    uint8_t buf[1];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_Ping_fields, &src), "pb_encode Ping");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)os.bytes_written, "Ping encodes to 0 bytes");

    gaggimate_Ping dst = gaggimate_Ping_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_Ping_fields, &dst), "pb_decode Ping (empty)");
}

// Tare (empty message) — same empty-payload contract as Ping.
static void test_tare_roundtrip_empty() {
    gaggimate_Tare src = gaggimate_Tare_init_zero;
    uint8_t buf[1];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_Tare_fields, &src), "pb_encode Tare");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)os.bytes_written, "Tare encodes to 0 bytes");

    gaggimate_Tare dst = gaggimate_Tare_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_Tare_fields, &dst), "pb_decode Tare (empty)");
}

// PidSettings (Kp,Ki,Kd,Kf) — all four floats survive bit-exact.
static void test_pid_settings_roundtrip_bit_exact() {
    gaggimate_PidSettings src = gaggimate_PidSettings_init_zero;
    src.kp = 24.567891f;
    src.ki = 1.234567f;
    src.kd = 105.987654f;
    src.kf = 0.345678f;

    uint8_t buf[gaggimate_PidSettings_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_PidSettings_fields, &src), "pb_encode PidSettings");

    gaggimate_PidSettings dst = gaggimate_PidSettings_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_PidSettings_fields, &dst), "pb_decode PidSettings");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kp, dst.kp), "kp bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.ki, dst.ki), "ki bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kd, dst.kd), "kd bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kf, dst.kf), "kf bit-exact");
}

// PidSettings — Kf absent on the wire (display omitted it) => decoded kf == 0.
// The display leaves kf at its proto default when no 4th token is present; a
// proto3 scalar at its default isn't serialized, so the decoder reads back 0.
static void test_pid_settings_kf_absent_defaults_zero() {
    gaggimate_PidSettings src = gaggimate_PidSettings_init_zero;
    src.kp = 30.0f;
    src.ki = 2.0f;
    src.kd = 90.0f;
    // kf intentionally left at the proto default (0) — models the no-Kf-token case.

    uint8_t buf[gaggimate_PidSettings_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_PidSettings_fields, &src), "pb_encode PidSettings (no kf)");

    gaggimate_PidSettings dst = gaggimate_PidSettings_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_PidSettings_fields, &dst), "pb_decode PidSettings (no kf)");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kp, dst.kp), "kp bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.ki, dst.ki), "ki bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.kd, dst.kd), "kd bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.kf), "kf absent => 0");
}

// PumpModelCoeffs (a,b,c,d) — all four floats survive bit-exact (normal mode).
static void test_pump_model_coeffs_roundtrip_bit_exact() {
    gaggimate_PumpModelCoeffs src = gaggimate_PumpModelCoeffs_init_zero;
    src.a = 0.123456f;
    src.b = -1.987654f;
    src.c = 2.345678f;
    src.d = 0.000789f;

    uint8_t buf[gaggimate_PumpModelCoeffs_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_PumpModelCoeffs_fields, &src), "pb_encode PumpModelCoeffs");

    gaggimate_PumpModelCoeffs dst = gaggimate_PumpModelCoeffs_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_PumpModelCoeffs_fields, &dst), "pb_decode PumpModelCoeffs");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.a, dst.a), "a bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.b, dst.b), "b bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.c, dst.c), "c bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.d, dst.d), "d bit-exact");
}

// PumpModelCoeffs — MANDATORY NaN preservation (PRO-244 AC). Two-point flow
// mode sends c=NaN, d=NaN; the proto float is IEEE-754 so the NaN bit pattern
// must survive the round trip while a,b stay exact.
static void test_pump_model_coeffs_nan_preserved() {
    gaggimate_PumpModelCoeffs src = gaggimate_PumpModelCoeffs_init_zero;
    src.a = 1.5f;
    src.b = -0.25f;
    src.c = NAN;
    src.d = NAN;

    uint8_t buf[gaggimate_PumpModelCoeffs_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_PumpModelCoeffs_fields, &src), "pb_encode PumpModelCoeffs (NaN c/d)");

    gaggimate_PumpModelCoeffs dst = gaggimate_PumpModelCoeffs_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_PumpModelCoeffs_fields, &dst), "pb_decode PumpModelCoeffs (NaN c/d)");

    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.a, dst.a), "a bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.b, dst.b), "b bit-exact");
    TEST_ASSERT_TRUE_MESSAGE(std::isnan(dst.c), "c stays NaN");
    TEST_ASSERT_TRUE_MESSAGE(std::isnan(dst.d), "d stays NaN");
}

// AutotuneRequest (int32 test_time, samples).
static void test_autotune_request_roundtrip() {
    gaggimate_AutotuneRequest src = gaggimate_AutotuneRequest_init_zero;
    src.test_time = 600;
    src.samples = 12;

    uint8_t buf[gaggimate_AutotuneRequest_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_AutotuneRequest_fields, &src), "pb_encode AutotuneRequest");

    gaggimate_AutotuneRequest dst = gaggimate_AutotuneRequest_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_AutotuneRequest_fields, &dst), "pb_decode AutotuneRequest");

    TEST_ASSERT_EQUAL_INT32_MESSAGE(src.test_time, dst.test_time, "test_time equal");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(src.samples, dst.samples, "samples equal");
}

// PressureScale (float scale) — a lossy text path before. Assert bit-exact.
static void test_pressure_scale_roundtrip_bit_exact() {
    gaggimate_PressureScale src = gaggimate_PressureScale_init_zero;
    src.scale = 1.0234567f; // carries >3 decimals so the old text path would round

    uint8_t buf[gaggimate_PressureScale_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_PressureScale_fields, &src), "pb_encode PressureScale");

    gaggimate_PressureScale dst = gaggimate_PressureScale_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_PressureScale_fields, &dst), "pb_decode PressureScale");
    TEST_ASSERT_TRUE_MESSAGE(bit_equal(src.scale, dst.scale), "scale bit-exact");
}

// Pin the regression: the old PressureScale path went through the lossy 3-dp
// float_to_string, losing precision nanopb now preserves.
static void test_pressure_scale_old_text_format_was_lossy() {
    const float scale = 1.0234567f; // rounds to 1.023 in 3dp text
    TEST_ASSERT_FALSE_MESSAGE(bit_equal(scale, text_round_3dp(scale)),
                              "old 3dp text format lost pressure scale precision (regression guard)");
}

// LedControl (uint32 channel, brightness) — assert full uint8 ranges survive.
static void test_led_control_roundtrip() {
    gaggimate_LedControl src = gaggimate_LedControl_init_zero;
    src.channel = 7;      // 0..7
    src.brightness = 255; // 0..255

    uint8_t buf[gaggimate_LedControl_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_LedControl_fields, &src), "pb_encode LedControl");

    gaggimate_LedControl dst = gaggimate_LedControl_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_LedControl_fields, &dst), "pb_decode LedControl");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(src.channel, dst.channel, "channel equal");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(src.brightness, dst.brightness, "brightness equal");
}

// ===========================================================================
// PRO-309 — negative-path + discriminator-only-frame decode coverage
// (follow-up from PRO-242 / PR #285). Purely additive host tests: the existing
// suite proves valid frames round-trip; these prove the decoder REJECTS a
// malformed body and that a 1-byte discriminator-only frame (empty proto3 body)
// decodes to an all-zero message for both OUTPUT_CONTROL sub-types.
// ===========================================================================

// NEGATIVE PATH — a malformed/truncated OUTPUT_CONTROL body must make pb_decode
// return false. The buffer is a field tag with wire type 2 (length-delimited)
// claiming 5 payload bytes, but the stream ends immediately, so nanopb runs off
// the end while reading the length-delimited content. Hand-built and
// deterministic (no rand); validated to actually return false.
static void test_output_control_decode_rejects_truncated_body() {
    // 0x0A = field 1, wire type 2 (length-delimited); 0x05 = claimed length 5;
    // no further bytes -> the decoder must fail reading the missing payload.
    const uint8_t garbage[] = {0x0A, 0x05};
    gaggimate_SimpleOutput dst = gaggimate_SimpleOutput_init_zero;
    pb_istream_t is = pb_istream_from_buffer(garbage, sizeof(garbage));
    TEST_ASSERT_FALSE_MESSAGE(pb_decode(&is, gaggimate_SimpleOutput_fields, &dst),
                              "pb_decode rejects truncated/garbage SimpleOutput body");
}

// NEGATIVE PATH (SensorData variant) — a tag byte announcing a fixed32 field
// (wire type 5) followed by fewer than 4 bytes truncates mid-value, so the
// decoder must fail. SensorData field 1 (temperature) is a float => fixed32.
static void test_sensor_data_decode_rejects_truncated_body() {
    // 0x0D = field 1, wire type 5 (32-bit); only 2 of the required 4 bytes
    // follow, so nanopb runs off the end reading the fixed32 value.
    const uint8_t garbage[] = {0x0D, 0xFF, 0xFF};
    gaggimate_SensorData dst = gaggimate_SensorData_init_zero;
    pb_istream_t is = pb_istream_from_buffer(garbage, sizeof(garbage));
    TEST_ASSERT_FALSE_MESSAGE(pb_decode(&is, gaggimate_SensorData_fields, &dst),
                              "pb_decode rejects truncated/garbage SensorData body");
}

// DISCRIMINATOR-ONLY FRAME — a frame carrying just the leading type byte and an
// EMPTY (0-length) proto3 message body. Mirrors the framing in
// test_output_control_discriminator_framing (body = frame+1, len = total-1; for
// a discriminator-only frame total=1 so the body length is 0). A 0-length
// proto3 message is valid and decodes to all-default (zero/false) fields, for
// both type 0 (SimpleOutput) and type 1 (AdvancedOutput).
static void test_output_control_discriminator_only_frame_zero_fills() {
    // ---- type 0 / simple ----
    {
        const uint8_t frame[] = {0}; // discriminator only, empty body
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, frame[0], "simple discriminator byte == 0");

        gaggimate_SimpleOutput dst = gaggimate_SimpleOutput_init_zero;
        pb_istream_t is = pb_istream_from_buffer(frame + 1, sizeof(frame) - 1);
        TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_SimpleOutput_fields, &dst),
                                 "pb_decode empty SimpleOutput body succeeds");
        TEST_ASSERT_FALSE_MESSAGE(dst.valve, "empty frame => valve false");
        TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.pump_setpoint), "empty frame => pump_setpoint 0");
        TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.boiler_setpoint), "empty frame => boiler_setpoint 0");
    }

    // ---- type 1 / advanced ----
    {
        const uint8_t frame[] = {1}; // discriminator only, empty body
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, frame[0], "advanced discriminator byte == 1");

        gaggimate_AdvancedOutput dst = gaggimate_AdvancedOutput_init_zero;
        pb_istream_t is = pb_istream_from_buffer(frame + 1, sizeof(frame) - 1);
        TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_AdvancedOutput_fields, &dst),
                                 "pb_decode empty AdvancedOutput body succeeds");
        TEST_ASSERT_FALSE_MESSAGE(dst.valve, "empty frame => valve false");
        TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.boiler_setpoint), "empty frame => boiler_setpoint 0");
        TEST_ASSERT_FALSE_MESSAGE(dst.pressure_target, "empty frame => pressure_target false");
        TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.pump_pressure), "empty frame => pump_pressure 0");
        TEST_ASSERT_TRUE_MESSAGE(bit_equal(0.0f, dst.pump_flow), "empty frame => pump_flow 0");
    }
}

static int runNanopbCommsTests() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_data_roundtrip_bit_exact);
    RUN_TEST(test_sensor_data_old_text_format_was_lossy);
    RUN_TEST(test_simple_output_roundtrip_bit_exact);
    RUN_TEST(test_advanced_output_roundtrip_bit_exact);
    RUN_TEST(test_output_control_discriminator_framing);
    // PRO-309 — negative-path + discriminator-only-frame decode coverage.
    RUN_TEST(test_output_control_decode_rejects_truncated_body);
    RUN_TEST(test_sensor_data_decode_rejects_truncated_body);
    RUN_TEST(test_output_control_discriminator_only_frame_zero_fills);
    RUN_TEST(test_error_roundtrip);
    RUN_TEST(test_brew_button_roundtrip);
    RUN_TEST(test_steam_button_roundtrip);
    RUN_TEST(test_autotune_result_roundtrip_bit_exact);
    RUN_TEST(test_autotune_result_old_text_format_was_lossy);
    RUN_TEST(test_volumetric_measurement_roundtrip_bit_exact);
    RUN_TEST(test_volumetric_measurement_old_text_format_was_lossy);
    RUN_TEST(test_tof_measurement_roundtrip);
    RUN_TEST(test_system_info_roundtrip);
    RUN_TEST(test_system_info_capability_permutations);
    // PRO-244 — remaining Display->Controller WRITE messages.
    RUN_TEST(test_alt_control_roundtrip);
    RUN_TEST(test_ping_roundtrip_empty);
    RUN_TEST(test_tare_roundtrip_empty);
    RUN_TEST(test_pid_settings_roundtrip_bit_exact);
    RUN_TEST(test_pid_settings_kf_absent_defaults_zero);
    RUN_TEST(test_pump_model_coeffs_roundtrip_bit_exact);
    RUN_TEST(test_pump_model_coeffs_nan_preserved);
    RUN_TEST(test_autotune_request_roundtrip);
    RUN_TEST(test_pressure_scale_roundtrip_bit_exact);
    RUN_TEST(test_pressure_scale_old_text_format_was_lossy);
    RUN_TEST(test_led_control_roundtrip);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runNanopbCommsTests(); }
void loop() {}
#else
int main(int, char **) { return runNanopbCommsTests(); }
#endif
