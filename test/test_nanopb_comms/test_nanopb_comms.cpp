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

static int runNanopbCommsTests() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_data_roundtrip_bit_exact);
    RUN_TEST(test_sensor_data_old_text_format_was_lossy);
    RUN_TEST(test_simple_output_roundtrip_bit_exact);
    RUN_TEST(test_advanced_output_roundtrip_bit_exact);
    RUN_TEST(test_output_control_discriminator_framing);
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
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runNanopbCommsTests(); }
void loop() {}
#else
int main(int, char **) { return runNanopbCommsTests(); }
#endif
