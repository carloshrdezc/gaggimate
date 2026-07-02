// PRO-239 nanopb spike — host round-trip test.
//
// Proves: a struct -> pb_encode -> bytes -> pb_decode -> struct cycle is
// LOSSLESS for the two hottest characteristics (SENSOR_DATA, OUTPUT_CONTROL
// simple_output), and contrasts that with the CURRENT text format's
// float_to_string() 3-decimal rounding, which is lossy.
//
// This is a standalone g++ harness (NOT pio test -e native) — see findings.md
// §2 for why: pio's native env compiles ArduinoJson + production logic and
// would need a generator pre-step wired in; a 1-file g++ harness against the
// vendored nanopb runtime answers the round-trip question with far less setup,
// which is the right tradeoff for a throwaway spike. Build/run via run.sh.

#include "comms.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Reproduce the CURRENT fork wire format exactly (NimBLEComm.h:70 +
// NimBLEServerController.cpp:90-98 / NimBLEClientController.cpp:183-190).
// ---------------------------------------------------------------------------
static std::string float_to_string(float f) {
    // VERBATIM from lib/NimBLEComm/src/NimBLEComm.h:70
    return std::to_string(std::round(f * 1000.0f) / 1000.0f);
}

static std::string text_encode_sensor(float t, float p, float pf, float mf, float pr) {
    return float_to_string(t) + "," + float_to_string(p) + "," + float_to_string(pf) + "," + float_to_string(mf) + "," +
           float_to_string(pr);
}

// get_token() is positional; toFloat() ~= atof on the substring. For the
// round-trip-loss check we just re-parse with atof on the comma split.
static float text_token_float(const std::string &s, int index) {
    int cur = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            if (cur == index)
                return atof(s.substr(start, i - start).c_str());
            cur++;
            start = i + 1;
        }
    }
    return 0.0f;
}

static int g_failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        printf("  FAIL: %s\n", what);
        g_failures++;
    }
}

// bit-exact float equality (the whole point: nanopb preserves the IEEE-754 bits)
static bool bit_equal(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// ---------------------------------------------------------------------------
// Test 1: SensorData (5 floats) — the hottest packet.
// ---------------------------------------------------------------------------
static void test_sensor_data() {
    printf("SensorData round-trip (5 floats):\n");

    // Representative live values, deliberately chosen with >3 decimal digits of
    // precision so the text format's rounding is observable.
    gaggimate_spike_SensorData src = gaggimate_spike_SensorData_init_zero;
    src.temperature = 93.456789f;
    src.pressure = 9.123456f;
    src.puck_flow = 2.718281f;
    src.pump_flow = 3.141592f;
    src.puck_resistance = 0.0007654321f;

    uint8_t buf[64];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    bool ok = pb_encode(&os, gaggimate_spike_SensorData_fields, &src);
    check(ok, "pb_encode SensorData");
    size_t encoded_len = os.bytes_written;
    printf("  nanopb encoded length: %zu bytes (max-size define = %d)\n", encoded_len, gaggimate_spike_SensorData_size);

    gaggimate_spike_SensorData dst = gaggimate_spike_SensorData_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, encoded_len);
    ok = pb_decode(&is, gaggimate_spike_SensorData_fields, &dst);
    check(ok, "pb_decode SensorData");

    // LOSSLESS: bit-exact equality on every field.
    check(bit_equal(src.temperature, dst.temperature), "temperature bit-exact");
    check(bit_equal(src.pressure, dst.pressure), "pressure bit-exact");
    check(bit_equal(src.puck_flow, dst.puck_flow), "puck_flow bit-exact");
    check(bit_equal(src.pump_flow, dst.pump_flow), "pump_flow bit-exact");
    check(bit_equal(src.puck_resistance, dst.puck_resistance), "puck_resistance bit-exact");
    printf("  nanopb: all 5 fields bit-exact after round-trip.\n");

    // Contrast: the CURRENT text format quantizes to 3 decimals and loses data.
    std::string text = text_encode_sensor(src.temperature, src.pressure, src.puck_flow, src.pump_flow, src.puck_resistance);
    float t_back = text_token_float(text, 0);
    float pr_back = text_token_float(text, 4);
    printf("  text format wire bytes: %zu  -> \"%s\"\n", text.size(), text.c_str());
    printf("  text temperature: in=%.7f out=%.7f  (delta=%.7f)\n", src.temperature, t_back, src.temperature - t_back);
    printf("  text puck_resistance: in=%.10f out=%.10f  (delta=%.10f)\n", src.puck_resistance, pr_back,
           src.puck_resistance - pr_back);
    check(!bit_equal(src.temperature, t_back), "text format IS lossy on temperature (expected)");
    // puck_resistance ~0.000765 rounds to 0.001 in text -> dramatic loss.
    check(std::fabs(src.puck_resistance - pr_back) > 1e-5f, "text format IS lossy on small puck_resistance (expected)");
    printf("  text format: temperature and small puck_resistance corrupted by 3-decimal rounding.\n\n");
}

// ---------------------------------------------------------------------------
// Test 2: SimpleOutput (bool + 2 floats) — the second hottest packet.
// ---------------------------------------------------------------------------
static void test_simple_output() {
    printf("SimpleOutput round-trip (bool + 2 floats):\n");

    gaggimate_spike_SimpleOutput src = gaggimate_spike_SimpleOutput_init_zero;
    src.valve = true;
    src.pump_setpoint = 87.654321f;
    src.boiler_setpoint = 93.333333f;

    uint8_t buf[32];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    bool ok = pb_encode(&os, gaggimate_spike_SimpleOutput_fields, &src);
    check(ok, "pb_encode SimpleOutput");
    size_t encoded_len = os.bytes_written;
    printf("  nanopb encoded length: %zu bytes (max-size define = %d)\n", encoded_len, gaggimate_spike_SimpleOutput_size);

    gaggimate_spike_SimpleOutput dst = gaggimate_spike_SimpleOutput_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, encoded_len);
    ok = pb_decode(&is, gaggimate_spike_SimpleOutput_fields, &dst);
    check(ok, "pb_decode SimpleOutput");

    check(src.valve == dst.valve, "valve equal");
    check(bit_equal(src.pump_setpoint, dst.pump_setpoint), "pump_setpoint bit-exact");
    check(bit_equal(src.boiler_setpoint, dst.boiler_setpoint), "boiler_setpoint bit-exact");
    printf("  nanopb: valve + both setpoints bit-exact after round-trip.\n");

    // The CURRENT simple_output text format does NOT round the setpoints — it
    // uses std::to_string(float) which prints 6 decimals. So the lossiness
    // problem is specific to telemetry sent through float_to_string()
    // (SensorData, AutotuneResult, VolumetricMeasurement, PressureScale).
    printf("  note: current simple_output text uses std::to_string (6dp), not the\n");
    printf("        3dp float_to_string — so OUTPUT_CONTROL is not the lossy path;\n");
    printf("        SensorData telemetry is. nanopb fixes both uniformly.\n\n");
}

int main() {
    printf("=== PRO-239 nanopb round-trip spike ===\n\n");
    test_sensor_data();
    test_simple_output();
    if (g_failures == 0) {
        printf("RESULT: PASS — round-trip lossless for both hot characteristics.\n");
        return 0;
    }
    printf("RESULT: FAIL — %d assertion(s) failed.\n", g_failures);
    return 1;
}
