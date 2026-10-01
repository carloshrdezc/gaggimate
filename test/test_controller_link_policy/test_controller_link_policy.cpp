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

void test_mismatch_forces_standby_even_when_startup_is_brew(void) {
    TEST_ASSERT_TRUE(shouldActivateStandbyOnReady(false, true));
    TEST_ASSERT_TRUE(shouldActivateStandbyOnReady(true, true));
    TEST_ASSERT_TRUE(shouldActivateStandbyOnReady(true, false));
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

// PRO-670: leaving standby is gated on controlAllowed; entering standby never is.
void test_leave_standby_gated_on_control_allowed(void) {
    // standby -> BREW/STEAM/... refused under mismatch / incompatible / no link
    TEST_ASSERT_FALSE(modeChangeAllowed(true, false, false));
    TEST_ASSERT_TRUE(modeChangeAllowed(true, false, true));
    // entering / staying in standby always allowed (stop, OTA, disconnect, error)
    TEST_ASSERT_TRUE(modeChangeAllowed(false, true, false));
    TEST_ASSERT_TRUE(modeChangeAllowed(true, true, false));
    TEST_ASSERT_TRUE(modeChangeAllowed(false, true, true));
    // not starting from standby: unaffected by this gate
    TEST_ASSERT_TRUE(modeChangeAllowed(false, false, false));
    TEST_ASSERT_TRUE(modeChangeAllowed(false, false, true));
}

void test_leave_standby_composed_with_link_state(void) {
    // Mismatched SystemInfo, incompatible (legacy, never SystemInfo), no link.
    TEST_ASSERT_FALSE(modeChangeAllowed(true, false, controlAllowed(true, true, isProtocolMismatch(5, LOCAL))));
    TEST_ASSERT_FALSE(modeChangeAllowed(true, false, controlAllowed(true, false, false)));
    TEST_ASSERT_FALSE(modeChangeAllowed(true, false, controlAllowed(false, false, false)));
    TEST_ASSERT_TRUE(modeChangeAllowed(true, false, controlAllowed(true, true, isProtocolMismatch(6, LOCAL))));
    TEST_ASSERT_TRUE(modeChangeAllowed(false, true, controlAllowed(true, true, true)));
}

// PRO-671/673: Controller::onSystemInfo mode decision, full table.
void test_system_info_mode_action_table(void) {
    using A = SystemInfoModeAction;
    struct Row {
        bool loaded, forced, mismatch, standby, startupStandby;
        A expect;
        const char *why;
    };
    static const Row rows[] = {
        // First SystemInfo since boot (`forced` is never set yet).
        {false, false, false, false, false, A::None, "boot: match, startup=BREW"},
        {false, false, false, false, true, A::StartupStandby, "boot: match, startup=STANDBY"},
        {false, false, false, true, true, A::StartupStandby, "boot: match, startup=STANDBY (already standby)"},
        {false, false, true, false, false, A::StartupStandby, "boot: mismatch forces standby despite startup=BREW"},
        {false, false, true, true, true, A::StartupStandby, "boot: mismatch, startup=STANDBY"},
        // Late mismatch.
        {true, false, true, false, false, A::ForceStandby, "late mismatch while BREW"},
        {true, false, true, false, true, A::ForceStandby, "late mismatch while BREW, startup=STANDBY"},
        {true, false, true, true, false, A::None, "late mismatch already in (non-mismatch) standby"},
        {true, true, true, true, false, A::None, "still mismatched after reconnect"},
        // Matching reconnects.
        {true, true, false, true, false, A::RestoreStartup, "corrected controller restores startup=BREW"},
        {true, true, false, true, true, A::None, "corrected controller, startup=STANDBY: stay"},
        {true, false, false, true, false, A::None, "REGRESSION: ordinary reconnect in STANDBY, startup=BREW"},
        {true, false, false, true, true, A::None, "ordinary reconnect in STANDBY, startup=STANDBY"},
        {true, false, false, false, false, A::None, "ordinary SystemInfo while BREW"},
        {true, true, false, false, false, A::None, "forced flag stale while not standby: no-op"},
    };
    for (const Row &r : rows) {
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(r.expect),
                                  static_cast<int>(systemInfoModeAction(r.loaded, r.forced, r.mismatch, r.standby, r.startupStandby)),
                                  r.why);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_system_info_mode_action_table);
    RUN_TEST(test_leave_standby_gated_on_control_allowed);
    RUN_TEST(test_leave_standby_composed_with_link_state);
    RUN_TEST(test_mismatch_detection);
    RUN_TEST(test_control_needs_connected_matching_systeminfo);
    RUN_TEST(test_legacy_controller_never_gets_control);
    RUN_TEST(test_ping_policy);
    RUN_TEST(test_mismatch_forces_standby_even_when_startup_is_brew);
    RUN_TEST(test_kicker_message_names_older_side);
    RUN_TEST(test_waiting_when_link_up_but_no_systeminfo);
    RUN_TEST(test_control_send_on_change_force_or_keepalive);
    return UNITY_END();
}
