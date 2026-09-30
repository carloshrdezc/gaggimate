#include "../../src/display/core/ControllerLinkPolicy.h"
#include <cstring>
#include <unity.h>

// PRO-655 R3/R4b: display-side mismatch / control-inhibit policy that
// Controller::onSystemInfo/updateControl/loop/setPid*/tare/autotune and the LVGL
// DefaultUI kicker dispatch on. v6 = upstream/master PROTOCOL_VERSION.
using namespace controller_link;
static constexpr uint32_t LOCAL = 6;

void setUp(void) {}
void tearDown(void) {}

void test_mismatch_detection(void) {
    TEST_ASSERT_FALSE(isProtocolMismatch(6, LOCAL));
    TEST_ASSERT_TRUE(isProtocolMismatch(5, LOCAL)); // stock v1.9.0 controller
    TEST_ASSERT_TRUE(isProtocolMismatch(7, LOCAL)); // newer controller
    TEST_ASSERT_TRUE(isProtocolMismatch(0, LOCAL)); // legacy / missing TX-RX (R3)
}

void test_control_needs_connected_matching_systeminfo(void) {
    TEST_ASSERT_TRUE(controlAllowed(true, true, false));
    TEST_ASSERT_FALSE(controlAllowed(false, true, false)); // link down
    TEST_ASSERT_FALSE(controlAllowed(true, false, false)); // no SystemInfo yet
    TEST_ASSERT_FALSE(controlAllowed(true, true, true));   // mismatch: OTA only
}

void test_legacy_controller_never_gets_control(void) {
    // onIncompatibleController -> onSystemInfo(..., protocolVersion = 0, ...)
    const bool mismatch = isProtocolMismatch(0, LOCAL);
    TEST_ASSERT_FALSE(controlAllowed(true, true, mismatch));
    TEST_ASSERT_FALSE(shouldSendPing(true, true, mismatch));
}

void test_ping_policy(void) {
    TEST_ASSERT_TRUE(shouldSendPing(true, false, false)); // handshake before SystemInfo
    TEST_ASSERT_TRUE(shouldSendPing(true, true, false));
    TEST_ASSERT_FALSE(shouldSendPing(true, true, true)); // controller watchdog keeps outputs off
    TEST_ASSERT_FALSE(shouldSendPing(false, false, false));
}

void test_startup_standby_skipped_on_mismatch(void) {
    TEST_ASSERT_TRUE(shouldActivateStandbyOnReady(false, true));
    TEST_ASSERT_FALSE(shouldActivateStandbyOnReady(true, true));
    TEST_ASSERT_FALSE(shouldActivateStandbyOnReady(false, false));
}

void test_kicker_message_names_older_side(void) {
    TEST_ASSERT_NOT_NULL(std::strstr(mismatchKickerMessage(7, LOCAL), "UPDATE DISPLAY"));
    TEST_ASSERT_NOT_NULL(std::strstr(mismatchKickerMessage(5, LOCAL), "UPDATE CONTROLLER"));
    TEST_ASSERT_NOT_NULL(std::strstr(mismatchKickerMessage(0, LOCAL), "UPDATE CONTROLLER"));
    TEST_ASSERT_NOT_NULL(std::strstr(mismatchKickerMessage(0, LOCAL), "VERSION MISMATCH"));
}

void test_waiting_when_link_up_but_no_systeminfo(void) {
    const uint32_t T = 10000;
    // B-P3-1: connected, SystemInfo never arrives -> waiting after the timeout.
    TEST_ASSERT_FALSE(shouldEnterWaiting(false, true, true, false, T, T));
    TEST_ASSERT_TRUE(shouldEnterWaiting(false, true, true, false, T + 1, T));
    // No link at all (legacy behaviour kept).
    TEST_ASSERT_TRUE(shouldEnterWaiting(false, true, false, false, T + 1, T));
    // Healthy link never waits.
    TEST_ASSERT_FALSE(shouldEnterWaiting(false, true, true, true, 60000, T));
    // Edge-triggered and only after init.
    TEST_ASSERT_FALSE(shouldEnterWaiting(true, true, true, false, T + 1, T));
    TEST_ASSERT_FALSE(shouldEnterWaiting(false, false, true, false, T + 1, T));
    // Wrap-safe elapsed from the caller: (uint32_t)(0x10 - 0xFFFFFF00) = 0x110.
    TEST_ASSERT_FALSE(shouldEnterWaiting(false, true, true, false, static_cast<uint32_t>(0x10u - 0xFFFFFF00u), T));
}

void test_control_send_on_change_force_or_keepalive(void) {
    const uint32_t K = 1000;
    TEST_ASSERT_FALSE(shouldSendControl(false, false, 100, K)); // steady: suppressed
    TEST_ASSERT_TRUE(shouldSendControl(true, false, 100, K));   // any part changed
    TEST_ASSERT_TRUE(shouldSendControl(false, true, 0, K));     // new link / SystemInfo
    TEST_ASSERT_TRUE(shouldSendControl(false, false, K, K));    // keepalive
    // Wrap-safe elapsed from the caller.
    TEST_ASSERT_FALSE(shouldSendControl(false, false, static_cast<uint32_t>(0x100u - 0xFFFFFE00u), K)); // 768 ms
    TEST_ASSERT_TRUE(shouldSendControl(false, false, static_cast<uint32_t>(0x400u - 0xFFFFFC00u), K));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_mismatch_detection);
    RUN_TEST(test_control_needs_connected_matching_systeminfo);
    RUN_TEST(test_legacy_controller_never_gets_control);
    RUN_TEST(test_ping_policy);
    RUN_TEST(test_startup_standby_skipped_on_mismatch);
    RUN_TEST(test_kicker_message_names_older_side);
    RUN_TEST(test_waiting_when_link_up_but_no_systeminfo);
    RUN_TEST(test_control_send_on_change_force_or_keepalive);
    return UNITY_END();
}
