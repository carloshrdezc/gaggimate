// PRO-596: pure, host-testable coverage for the cloud-relay connection-parameter
// derivation extracted from the inline file-static `parseRelayUrl` /
// `relayTokenProtocol` (and the connect-path ternary) in WebUIPlugin.cpp, which
// previously had zero host test coverage because WebUIPlugin.cpp is not in the
// [env:native] build_src_filter.
//
// Covers:
//   - parseRelayUrl: ws:// vs wss:// scheme + default ports (80 / 443), explicit
//     port, host-only vs host+path, root vs nested base path, and invalid /
//     unrecognized-scheme rejection.
//   - relayArduinoToInt: leading-integer parse + non-numeric -> 0 (Arduino
//     String::toInt() semantics the original relied on for the port).
//   - resolveRelayConnectPath: empty / "/" -> absolute path; nested base path
//     prefix.
//   - relayTokenProtocol: URL-safe base64 (no padding) of the raw token bytes
//     with the "gaggimate-token-" prefix, including 1-, 2-, and 3-byte tails
//     and the empty token.
//
// Header-only + free of any Arduino-String method (uses std::string /
// const char*), so it links on [env:native] via the existing `-I src`,
// mirroring the OtaIntentState.h / OtaChannelSwitchPolicy.h precedent.

#include "../../src/display/plugins/RelayConnectionPolicy.h"
#include <unity.h>

using namespace relay_connection_policy;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// parseRelayUrl
// ---------------------------------------------------------------------------

void test_parse_ws_host_only_default_port(void) {
    const RelayUrlParts p = parseRelayUrl("ws://relay.example.com");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_FALSE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("relay.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(80, p.port);
    TEST_ASSERT_EQUAL_STRING("/", p.basePath.c_str());
}

void test_parse_wss_host_only_default_port(void) {
    const RelayUrlParts p = parseRelayUrl("wss://relay.example.com");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("relay.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(443, p.port);
    TEST_ASSERT_EQUAL_STRING("/", p.basePath.c_str());
}

void test_parse_explicit_port(void) {
    const RelayUrlParts p = parseRelayUrl("ws://192.168.1.10:8080");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_FALSE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(8080, p.port);
    TEST_ASSERT_EQUAL_STRING("/", p.basePath.c_str());
}

void test_parse_wss_explicit_port_and_path(void) {
    const RelayUrlParts p = parseRelayUrl("wss://relay.example.com:9443/relay/v1");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("relay.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(9443, p.port);
    TEST_ASSERT_EQUAL_STRING("/relay/v1", p.basePath.c_str());
}

void test_parse_path_without_explicit_port_uses_default(void) {
    const RelayUrlParts p = parseRelayUrl("ws://host.local/base");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_FALSE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("host.local", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(80, p.port);
    TEST_ASSERT_EQUAL_STRING("/base", p.basePath.c_str());
}

void test_parse_invalid_scheme_returns_invalid(void) {
    TEST_ASSERT_FALSE(parseRelayUrl("http://relay.example.com").valid);
    TEST_ASSERT_FALSE(parseRelayUrl("relay.example.com").valid);
    TEST_ASSERT_FALSE(parseRelayUrl("").valid);
    // "ws" / "wss" without "://" must not match (rfind at pos 0 needs the scheme).
    TEST_ASSERT_FALSE(parseRelayUrl("wsx://relay.example.com").valid);
}

// ---------------------------------------------------------------------------
// relayArduinoToInt (Arduino String::toInt() semantics)
// ---------------------------------------------------------------------------

void test_arduino_to_int_parses_leading_integer(void) {
    TEST_ASSERT_EQUAL_INT(8080, (int)relayArduinoToInt("8080"));
    // Stops at the first non-digit, mirroring toInt().
    TEST_ASSERT_EQUAL_INT(80, (int)relayArduinoToInt("80abc"));
}

void test_arduino_to_int_non_numeric_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, (int)relayArduinoToInt(""));
    TEST_ASSERT_EQUAL_INT(0, (int)relayArduinoToInt("abc"));
}

void test_parse_non_numeric_port_yields_zero(void) {
    // toInt() of a non-numeric port is 0; the original cast that to uint16_t.
    const RelayUrlParts p = parseRelayUrl("ws://host:abc/path");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_STRING("host", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(0, p.port);
    TEST_ASSERT_EQUAL_STRING("/path", p.basePath.c_str());
}

void test_parse_whitespace_prefixed_explicit_port(void) {
    // Regression (PRO-596): a space after the colon must still yield the port.
    // Arduino String::toInt() -> atol -> strtol skips leading whitespace, so
    // " 8080" parses to 8080. The prior hand-rolled parser bailed on the space
    // and returned 0, breaking this whitespace-prefixed explicit port.
    const RelayUrlParts p = parseRelayUrl("ws://relay.example.com: 8080/path");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_FALSE(p.useSSL);
    TEST_ASSERT_EQUAL_STRING("relay.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(8080, p.port);
    TEST_ASSERT_EQUAL_STRING("/path", p.basePath.c_str());
}

void test_parse_overflow_port_truncation_is_deterministic(void) {
    // Documents (pins) the out-of-range truncation policy for a port digit run
    // that overflows `long`. strtol saturates to LONG_MAX rather than wrapping,
    // and the call site's (uint16_t) cast keeps the low 16 bits. On the LP64
    // host LONG_MAX (0x7FFF'FFFF'FFFF'FFFF) truncates to 0xFFFF == 65535.
    // This test does not assert 65535 is "correct" — it pins whatever the
    // strtol-clamp + (uint16_t)-cast pipeline deterministically computes, and
    // asserts relayArduinoToInt itself returns the clamped LONG_MAX.
    TEST_ASSERT_EQUAL_INT64((long)0x7FFFFFFFFFFFFFFFLL, relayArduinoToInt("999999999999999999999"));
    const RelayUrlParts p = parseRelayUrl("ws://host:999999999999999999999/path");
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_STRING("host", p.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(65535, p.port);
    TEST_ASSERT_EQUAL_STRING("/path", p.basePath.c_str());
}

// ---------------------------------------------------------------------------
// resolveRelayConnectPath
// ---------------------------------------------------------------------------

void test_resolve_connect_path_empty_base(void) {
    TEST_ASSERT_EQUAL_STRING("/connect?role=device", resolveRelayConnectPath("").c_str());
}

void test_resolve_connect_path_root_base(void) {
    TEST_ASSERT_EQUAL_STRING("/connect?role=device", resolveRelayConnectPath("/").c_str());
}

void test_resolve_connect_path_nested_base(void) {
    TEST_ASSERT_EQUAL_STRING("/relay/v1/connect?role=device", resolveRelayConnectPath("/relay/v1").c_str());
}

// ---------------------------------------------------------------------------
// relayTokenProtocol (URL-safe base64, no padding, "gaggimate-token-" prefix)
// ---------------------------------------------------------------------------

void test_token_protocol_prefix_and_empty(void) {
    // Empty token -> just the prefix, no encoded chars.
    TEST_ASSERT_EQUAL_STRING("gaggimate-token-", relayTokenProtocol("").c_str());
}

void test_token_protocol_three_byte_group(void) {
    // "Man" is the canonical base64 example -> "TWFu" (full 4-char group).
    TEST_ASSERT_EQUAL_STRING("gaggimate-token-TWFu", relayTokenProtocol("Man").c_str());
}

void test_token_protocol_two_byte_tail_no_padding(void) {
    // "Ma" -> "TWE" (3 chars, no '=' padding).
    TEST_ASSERT_EQUAL_STRING("gaggimate-token-TWE", relayTokenProtocol("Ma").c_str());
}

void test_token_protocol_one_byte_tail_no_padding(void) {
    // "M" -> "TQ" (2 chars, no '=' padding).
    TEST_ASSERT_EQUAL_STRING("gaggimate-token-TQ", relayTokenProtocol("M").c_str());
}

void test_token_protocol_url_safe_alphabet(void) {
    // Bytes 0xFB 0xFF map to indices 62 (=> '-') and 63 (=> '_') in the URL-safe
    // alphabet, proving '-'/'_' are used instead of '+'/'/'.
    // 0xFB,0xFF -> block bits: 111110 111111 11.... -> chars 62,63,+2-bit tail.
    const std::string t = std::string("\xFB\xFF", 2);
    const std::string out = relayTokenProtocol(t);
    // Prefix + 3 chars (2-byte tail): index62='-', index63='_', tail=index56='8'.
    TEST_ASSERT_EQUAL_STRING("gaggimate-token--_8", out.c_str());
}

static int runRelayConnectionPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_ws_host_only_default_port);
    RUN_TEST(test_parse_wss_host_only_default_port);
    RUN_TEST(test_parse_explicit_port);
    RUN_TEST(test_parse_wss_explicit_port_and_path);
    RUN_TEST(test_parse_path_without_explicit_port_uses_default);
    RUN_TEST(test_parse_invalid_scheme_returns_invalid);
    RUN_TEST(test_arduino_to_int_parses_leading_integer);
    RUN_TEST(test_arduino_to_int_non_numeric_is_zero);
    RUN_TEST(test_parse_non_numeric_port_yields_zero);
    RUN_TEST(test_parse_whitespace_prefixed_explicit_port);
    RUN_TEST(test_parse_overflow_port_truncation_is_deterministic);
    RUN_TEST(test_resolve_connect_path_empty_base);
    RUN_TEST(test_resolve_connect_path_root_base);
    RUN_TEST(test_resolve_connect_path_nested_base);
    RUN_TEST(test_token_protocol_prefix_and_empty);
    RUN_TEST(test_token_protocol_three_byte_group);
    RUN_TEST(test_token_protocol_two_byte_tail_no_padding);
    RUN_TEST(test_token_protocol_one_byte_tail_no_padding);
    RUN_TEST(test_token_protocol_url_safe_alphabet);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runRelayConnectionPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runRelayConnectionPolicyTests(); }
#endif
