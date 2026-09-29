// PRO-655 increment 1: host codec tests for the vendored upstream
// lib/NanoPbComm (v1.9.0). Runs in [env:native-nanopbcomm] against the real
// gaggimate.proto, codegen'd at build time. Covers the pure layers only:
// Frame/Payload nanopb round-trips for every Payload oneof member, the
// missing protocol-version codec default, Protocol.h coalescing/priority, the
// coalescing priority queue, and the UART COBS/CRC framing. Endpoint and the
// BLE/UART transports are FreeRTOS/NimBLE-bound and are HIL-only.
//
// No Carlos-only proto fields exist (docs/pro-655-nanopb-comms-migration.md
// §2.3), so these tests pin the upstream wire contract Carlos's firmware will
// speak after increments 2-3.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <unity.h>

#include "CoalescingPriorityQueue.h"
#include "Protocol.h"
#include "uart/UartFraming.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

uint8_t buf[gaggimate_Frame_size];

size_t encodeFrame(const gm::Frame &f) {
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&os, gaggimate_Frame_fields, &f), PB_GET_ERROR(&os));
    return os.bytes_written;
}

gm::Frame decodeFrame(size_t len) {
    gm::Frame out = gaggimate_Frame_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&is, gaggimate_Frame_fields, &out), PB_GET_ERROR(&is));
    return out;
}

// Wrap one payload in a Frame, encode, decode, return the decoded payload.
gm::Payload roundTrip(const gm::Payload &p) {
    gm::Frame f = gaggimate_Frame_init_zero;
    f.id = 7;
    f.payloads_count = 1;
    f.payloads[0] = p;
    gm::Frame out = decodeFrame(encodeFrame(f));
    TEST_ASSERT_EQUAL_UINT32(7, out.id);
    TEST_ASSERT_EQUAL(1, out.payloads_count);
    TEST_ASSERT_EQUAL(p.which_content, out.payloads[0].which_content);
    return out.payloads[0];
}

gm::Payload make(pb_size_t tag) {
    gm::Payload p = gaggimate_Payload_init_zero;
    p.which_content = tag;
    return p;
}

} // namespace

// ---- Display -> Controller ----------------------------------------------

void test_ping_and_tare(void) {
    roundTrip(make(gaggimate_Payload_ping_tag));
    roundTrip(make(gaggimate_Payload_tare_tag));
}

void test_boiler_control(void) {
    gm::Payload p = make(gaggimate_Payload_boiler_tag);
    p.content.boiler = {1, gaggimate_BoilerMode_BOILER_MODE_PRESSURE, 93.25f};
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL_UINT32(1, o.content.boiler.index);
    TEST_ASSERT_EQUAL(gaggimate_BoilerMode_BOILER_MODE_PRESSURE, o.content.boiler.mode);
    TEST_ASSERT_EQUAL_FLOAT(93.25f, o.content.boiler.setpoint);
}

void test_pump_control(void) {
    gm::Payload p = make(gaggimate_Payload_pump_tag);
    p.content.pump = {0, gaggimate_PumpMode_PUMP_MODE_FLOW, 55.5f, 9.1f, 2.25f};
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL(gaggimate_PumpMode_PUMP_MODE_FLOW, o.content.pump.mode);
    TEST_ASSERT_EQUAL_FLOAT(55.5f, o.content.pump.power);
    TEST_ASSERT_EQUAL_FLOAT(9.1f, o.content.pump.pressure);
    TEST_ASSERT_EQUAL_FLOAT(2.25f, o.content.pump.flow);
}

void test_relay_control(void) {
    gm::Payload p = make(gaggimate_Payload_relay_tag);
    p.content.relay = {1, true};
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL_UINT32(1, o.content.relay.index);
    TEST_ASSERT_TRUE(o.content.relay.open);
}

void test_pid_and_pump_settings(void) {
    gm::Payload p = make(gaggimate_Payload_pid_tag);
    p.content.pid = {2.5f, 0.1f, 40.0f, 0.75f};
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, o.content.pid.kf);

    gm::Payload s = make(gaggimate_Payload_pump_model_tag);
    s.content.pump_model.a = 1.5f;
    s.content.pump_model.c = NAN; // Carlos sends NaN for unset coeffs
    s.content.pump_model.slipD = -0.25f;
    gm::Payload so = roundTrip(s);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, so.content.pump_model.a);
    TEST_ASSERT_TRUE(std::isnan(so.content.pump_model.c));
    TEST_ASSERT_EQUAL_FLOAT(-0.25f, so.content.pump_model.slipD);
}

void test_autotune_pressure_scale_led(void) {
    gm::Payload a = make(gaggimate_Payload_autotune_tag);
    a.content.autotune = {120, 4, 1350};
    TEST_ASSERT_EQUAL_UINT32(1350, roundTrip(a).content.autotune.heater_wattage);

    gm::Payload s = make(gaggimate_Payload_pressure_scale_tag);
    s.content.pressure_scale.scale = 16.0f;
    TEST_ASSERT_EQUAL_FLOAT(16.0f, roundTrip(s).content.pressure_scale.scale);

    gm::Payload l = make(gaggimate_Payload_led_tag);
    l.content.led.channels_count = 8; // PCA9634 max (gaggimate.options)
    for (uint32_t i = 0; i < 8; i++)
        l.content.led.channels[i] = {i, 255 - i};
    gm::Payload lo = roundTrip(l);
    TEST_ASSERT_EQUAL(8, lo.content.led.channels_count);
    TEST_ASSERT_EQUAL_UINT32(248, lo.content.led.channels[7].brightness);
}

// ---- Controller -> Display ----------------------------------------------

void test_system_info_and_protocol_version(void) {
    gm::Payload p = make(gaggimate_Payload_system_info_tag);
    std::strcpy(p.content.system_info.hardware, "GaggiMate Pro");
    std::strcpy(p.content.system_info.version, "v1.9.0-carlos");
    p.content.system_info.has_capabilities = true;
    p.content.system_info.capabilities.pressure = true;
    p.content.system_info.capabilities.tof = true;
    p.content.system_info.capabilities.addons_count = 1;
    p.content.system_info.capabilities.addons[0].type = 3;
    p.content.system_info.protocol_version = gm_proto::PROTOCOL_VERSION;
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL_STRING("GaggiMate Pro", o.content.system_info.hardware);
    TEST_ASSERT_EQUAL_STRING("v1.9.0-carlos", o.content.system_info.version);
    TEST_ASSERT_TRUE(o.content.system_info.capabilities.pressure);
    TEST_ASSERT_FALSE(o.content.system_info.capabilities.dimming);
    TEST_ASSERT_EQUAL_UINT32(3, o.content.system_info.capabilities.addons[0].type);
    TEST_ASSERT_EQUAL_UINT32(gm_proto::PROTOCOL_VERSION, o.content.system_info.protocol_version);
}

// Pins the vendored revision: the v1.9.0 final tree has PROTOCOL_VERSION = 5
// (upstream/master is 6). Changing this is a deliberate wire break.
void test_protocol_version_pinned(void) { TEST_ASSERT_EQUAL_UINT32(5, gm_proto::PROTOCOL_VERSION); }

// Codec default only: an omitted SystemInfo.protocol_version (field 4) decodes
// as the proto3 default 0, which can never equal a real PROTOCOL_VERSION. This
// does NOT exercise the production mismatch gate / control inhibition; that
// lives in the display Controller (increment 3) and is covered there + by HIL.
void test_codec_missing_protocol_version_defaults_to_zero(void) {
    gm::Payload p = make(gaggimate_Payload_system_info_tag);
    std::strcpy(p.content.system_info.version, "old");
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL_UINT32(0, o.content.system_info.protocol_version);
    TEST_ASSERT_TRUE(o.content.system_info.protocol_version != gm_proto::PROTOCOL_VERSION);
}

void test_sensor_data(void) {
    gm::Payload p = make(gaggimate_Payload_sensor_tag);
    p.content.sensor.boilers_count = 1;
    p.content.sensor.boilers[0] = {0, 93.5f, 9.0f, 0.4f};
    p.content.sensor.puck_flow = 1.8f;
    p.content.sensor.pump_flow = 2.1f;
    p.content.sensor.puck_resistance = 0.33f;
    p.content.sensor.pump_power = 70.0f;
    p.content.sensor.water_pumped = 42.5f;
    gm::Payload o = roundTrip(p);
    TEST_ASSERT_EQUAL(1, o.content.sensor.boilers_count);
    TEST_ASSERT_EQUAL_FLOAT(93.5f, o.content.sensor.boilers[0].temperature);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, o.content.sensor.boilers[0].pressure);
    TEST_ASSERT_EQUAL_FLOAT(0.33f, o.content.sensor.puck_resistance);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, o.content.sensor.water_pumped);
}

void test_button_autotune_volumetric_tof_error(void) {
    gm::Payload b = make(gaggimate_Payload_button_tag);
    b.content.button = {1, true}; // 1 = steam (Carlos SteamButton)
    gm::Payload bo = roundTrip(b);
    TEST_ASSERT_EQUAL_UINT32(1, bo.content.button.index);
    TEST_ASSERT_TRUE(bo.content.button.pressed);

    gm::Payload r = make(gaggimate_Payload_autotune_result_tag);
    r.content.autotune_result = {1.f, 2.f, 3.f, 4.f};
    TEST_ASSERT_EQUAL_FLOAT(4.f, roundTrip(r).content.autotune_result.kf);

    gm::Payload v = make(gaggimate_Payload_volumetric_tag);
    v.content.volumetric.volume = 36.4f;
    TEST_ASSERT_EQUAL_FLOAT(36.4f, roundTrip(v).content.volumetric.volume);

    gm::Payload t = make(gaggimate_Payload_tof_tag);
    t.content.tof.distance = 123;
    TEST_ASSERT_EQUAL_UINT32(123, roundTrip(t).content.tof.distance);

    gm::Payload e = make(gaggimate_Payload_error_tag);
    e.content.error.code = gaggimate_ErrorCode_ERROR_CODE_RUNAWAY;
    TEST_ASSERT_EQUAL(gaggimate_ErrorCode_ERROR_CODE_RUNAWAY, roundTrip(e).content.error.code);
}

// Carlos's ERROR_CODE_* ints (NimBLEComm.h) must map 1:1 onto the enum.
void test_error_codes_match_legacy_values(void) {
    TEST_ASSERT_EQUAL(1, gaggimate_ErrorCode_ERROR_CODE_COMM_SEND);
    TEST_ASSERT_EQUAL(2, gaggimate_ErrorCode_ERROR_CODE_COMM_RCV);
    TEST_ASSERT_EQUAL(3, gaggimate_ErrorCode_ERROR_CODE_PROTO_ERR);
    TEST_ASSERT_EQUAL(4, gaggimate_ErrorCode_ERROR_CODE_RUNAWAY);
    TEST_ASSERT_EQUAL(5, gaggimate_ErrorCode_ERROR_CODE_TIMEOUT);
    TEST_ASSERT_EQUAL(6, gaggimate_ErrorCode_ERROR_CODE_AUTOTUNE_TIMEOUT);
}

// ---- Framing ---------------------------------------------------------------

void test_full_batch_frame_with_ack(void) {
    gm::Frame f = gaggimate_Frame_init_zero;
    f.id = 0xFFFFFFFFu;
    f.ack = 41;
    f.payloads_count = 6; // max_count from gaggimate.options
    for (int i = 0; i < 6; i++) {
        f.payloads[i] = make(gaggimate_Payload_relay_tag);
        f.payloads[i].content.relay = {static_cast<uint32_t>(i), (i % 2) == 0};
    }
    size_t n = encodeFrame(f);
    TEST_ASSERT_TRUE(n <= gaggimate_Frame_size);
    gm::Frame o = decodeFrame(n);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, o.id);
    TEST_ASSERT_EQUAL_UINT32(41, o.ack);
    TEST_ASSERT_EQUAL(6, o.payloads_count);
    TEST_ASSERT_EQUAL_UINT32(5, o.payloads[5].content.relay.index);
}

void test_ack_only_frame_is_tiny(void) {
    gm::Frame f = gaggimate_Frame_init_zero;
    f.ack = 3;
    size_t n = encodeFrame(f);
    TEST_ASSERT_EQUAL(2, n); // tag 0x10 + varint 3
    TEST_ASSERT_EQUAL(0, decodeFrame(n).payloads_count);
}

void test_truncated_frame_fails_to_decode(void) {
    gm::Payload p = make(gaggimate_Payload_system_info_tag);
    std::strcpy(p.content.system_info.hardware, "GaggiMate");
    gm::Frame f = gaggimate_Frame_init_zero;
    f.payloads_count = 1;
    f.payloads[0] = p;
    size_t n = encodeFrame(f);
    gm::Frame out = gaggimate_Frame_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, n - 3);
    TEST_ASSERT_FALSE(pb_decode(&is, gaggimate_Frame_fields, &out));
}

// ---- Protocol.h coalescing / priority ----------------------------------

void test_coalescing_key_separates_family_and_index(void) {
    gm::Payload r0 = make(gaggimate_Payload_relay_tag);
    gm::Payload r1 = make(gaggimate_Payload_relay_tag);
    r1.content.relay.index = 1;
    gm::Payload b0 = make(gaggimate_Payload_boiler_tag);
    TEST_ASSERT_NOT_EQUAL(gm_proto::coalescingKey(r0), gm_proto::coalescingKey(r1));
    TEST_ASSERT_NOT_EQUAL(gm_proto::coalescingKey(r0), gm_proto::coalescingKey(b0));
    // Out-of-range index clamps into the family instead of aliasing another one.
    gm::Payload big = make(gaggimate_Payload_relay_tag);
    big.content.relay.index = 999;
    TEST_ASSERT_EQUAL(gaggimate_Payload_relay_tag * gm_proto::MAX_DEVICES + gm_proto::MAX_DEVICES - 1,
                      gm_proto::coalescingKey(big));
}

void test_default_priorities(void) {
    TEST_ASSERT_EQUAL(gm_proto::PRIO_HIGH, gm_proto::defaultPriority(gaggimate_Payload_ping_tag));
    TEST_ASSERT_EQUAL(gm_proto::PRIO_CONTROL, gm_proto::defaultPriority(gaggimate_Payload_boiler_tag));
    TEST_ASSERT_EQUAL(gm_proto::PRIO_LOW, gm_proto::defaultPriority(gaggimate_Payload_sensor_tag));
    TEST_ASSERT_EQUAL(gm_proto::PRIO_NORMAL, gm_proto::defaultPriority(gaggimate_Payload_pid_tag));
}

// ---- CoalescingPrioQueue -------------------------------------------------

void test_queue_orders_by_priority_and_coalesces(void) {
    CoalescingPrioQueue<4, uint16_t, uint32_t, 16> q;
    TEST_ASSERT_TRUE(q.upsert(1, 50, 100));
    TEST_ASSERT_TRUE(q.upsert(2, 200, 200));
    TEST_ASSERT_TRUE(q.upsert(1, 50, 101)); // coalesces key 1
    TEST_ASSERT_EQUAL(2, q.size());
    auto a = q.pop();
    TEST_ASSERT_TRUE(a.has_value());
    TEST_ASSERT_EQUAL_UINT16(2, a->key);
    auto b = q.pop();
    TEST_ASSERT_EQUAL_UINT32(101, b->payload); // latest value wins
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_FALSE(q.pop().has_value());
}

void test_queue_rejects_bad_key_and_invalidates(void) {
    CoalescingPrioQueue<2, uint16_t, uint32_t, 16> q;
    TEST_ASSERT_FALSE(q.upsert(16, 1, 0)); // key >= MaxKeys
    TEST_ASSERT_TRUE(q.upsert(3, 1, 0));
    TEST_ASSERT_TRUE(q.invalidate(3));
    TEST_ASSERT_FALSE(q.invalidate(3));
    TEST_ASSERT_TRUE(q.empty());
}

// ---- UART framing --------------------------------------------------------

void test_crc16_ccitt_false_check_value(void) {
    const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1, gm_uart::crc16(v, sizeof(v)));
}

void test_cobs_roundtrip_of_encoded_frame(void) {
    gm::Frame f = gaggimate_Frame_init_zero;
    f.id = 1;
    f.payloads_count = 1;
    f.payloads[0] = make(gaggimate_Payload_tof_tag); // encodes zero bytes
    size_t n = encodeFrame(f);
    uint8_t enc[gm_uart::cobsMaxEncodedLen(sizeof(buf))];
    size_t en = gm_uart::cobsEncode(buf, n, enc);
    TEST_ASSERT_TRUE(en <= gm_uart::cobsMaxEncodedLen(n));
    for (size_t i = 0; i < en; i++)
        TEST_ASSERT_NOT_EQUAL(0, enc[i]); // no delimiter inside the block
    uint8_t dec[sizeof(buf)];
    TEST_ASSERT_EQUAL(n, gm_uart::cobsDecode(enc, en, dec, sizeof(dec)));
    TEST_ASSERT_EQUAL_MEMORY(buf, dec, n);
    TEST_ASSERT_EQUAL(0, gm_uart::cobsDecode(enc, en, dec, n - 1)); // too small -> 0
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ping_and_tare);
    RUN_TEST(test_boiler_control);
    RUN_TEST(test_pump_control);
    RUN_TEST(test_relay_control);
    RUN_TEST(test_pid_and_pump_settings);
    RUN_TEST(test_autotune_pressure_scale_led);
    RUN_TEST(test_system_info_and_protocol_version);
    RUN_TEST(test_protocol_version_pinned);
    RUN_TEST(test_codec_missing_protocol_version_defaults_to_zero);
    RUN_TEST(test_sensor_data);
    RUN_TEST(test_button_autotune_volumetric_tof_error);
    RUN_TEST(test_error_codes_match_legacy_values);
    RUN_TEST(test_full_batch_frame_with_ack);
    RUN_TEST(test_ack_only_frame_is_tiny);
    RUN_TEST(test_truncated_frame_fails_to_decode);
    RUN_TEST(test_coalescing_key_separates_family_and_index);
    RUN_TEST(test_default_priorities);
    RUN_TEST(test_queue_orders_by_priority_and_coalesces);
    RUN_TEST(test_queue_rejects_bad_key_and_invalidates);
    RUN_TEST(test_crc16_ccitt_false_check_value);
    RUN_TEST(test_cobs_roundtrip_of_encoded_frame);
    return UNITY_END();
}
