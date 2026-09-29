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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_mismatch_detection);
    RUN_TEST(test_control_needs_connected_matching_systeminfo);
    RUN_TEST(test_legacy_controller_never_gets_control);
    RUN_TEST(test_ping_policy);
    RUN_TEST(test_startup_standby_skipped_on_mismatch);
    RUN_TEST(test_kicker_message_names_older_side);
    return UNITY_END();
}
