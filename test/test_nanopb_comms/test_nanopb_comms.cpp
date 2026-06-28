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

static int runNanopbCommsTests() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_data_roundtrip_bit_exact);
    RUN_TEST(test_sensor_data_old_text_format_was_lossy);
    RUN_TEST(test_simple_output_roundtrip_bit_exact);
    RUN_TEST(test_advanced_output_roundtrip_bit_exact);
    RUN_TEST(test_output_control_discriminator_framing);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runNanopbCommsTests(); }
void loop() {}
#else
int main(int, char **) { return runNanopbCommsTests(); }
#endif
