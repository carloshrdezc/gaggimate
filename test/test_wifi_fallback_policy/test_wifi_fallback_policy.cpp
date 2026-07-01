#include "display/core/WiFiFallbackPolicy.h"
#include <unity.h>

// PRO-333: HomeKit-enabled breaks WiFi STA, and the device never falls back to
// SoftAP after a previously-good connection drops.
//
// Controller::setupWifi() only opened the SoftAP when the *initial* STA connect
// failed. When HomeSpan's HAP server started it tore the live STA down
// (ASSOC_LEAVE -> AUTH_EXPIRE loop) under Arduino-esp32 3.x / IDF 5.x, so the
// device sat off the LAN AND off its own AP, locking the user out. The fix adds
// a per-loop watchdog whose pure decision lives in WiFiFallbackPolicy.h:
//   - within a grace window: let Arduino auto-reconnect heal blips
//   - past grace: re-assert STA ownership (counter HomeSpan's WiFi.begin churn)
//   - past the fallback window: open SoftAP so the web UI is always reachable
//
// The full Controller cannot be instantiated on the host ([env:native] does not
// shim WiFi/LVGL/FreeRTOS), so this pins the watchdog's decision contract.

void setUp(void) {}
void tearDown(void) {}

// While the STA is connected, the watchdog never acts regardless of timers.
void test_connected_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(/*staConnected*/ true, /*apMode*/ false,
                                                              /*staConfigured*/ true, /*downForMs*/ 999999,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// Already in AP mode: nothing to recover, the user already has a surface.
void test_ap_mode_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(false, /*apMode*/ true, true, 999999, WIFI_WATCHDOG_GRACE_MS,
                                                              WIFI_WATCHDOG_FALLBACK_MS)));
}

// No STA configured (no credentials): the device is meant to be in AP mode; the
// watchdog must not try to reconnect a STA that was never set up.
void test_unconfigured_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(false, false, /*staConfigured*/ false, 999999,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// STA down but still inside the grace window: hold off, let auto-reconnect try.
void test_down_within_grace_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(false, false, true, WIFI_WATCHDOG_GRACE_MS - 1,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// At exactly the grace boundary the watchdog re-asserts STA (>= boundary acts).
void test_down_at_grace_reconnects(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::RECONNECT),
                          static_cast<int>(wifiWatchdogAction(false, false, true, WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_GRACE_MS,
                                                              WIFI_WATCHDOG_FALLBACK_MS)));
}

// Between grace and fallback windows: still trying to re-assert STA, not AP yet.
void test_down_between_windows_reconnects(void) {
    const unsigned long mid = (WIFI_WATCHDOG_GRACE_MS + WIFI_WATCHDOG_FALLBACK_MS) / 2;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WiFiWatchdogAction::RECONNECT),
        static_cast<int>(wifiWatchdogAction(false, false, true, mid, WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// Just before the fallback boundary: still RECONNECT, not yet SoftAP.
void test_down_just_before_fallback_reconnects(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::RECONNECT),
                          static_cast<int>(wifiWatchdogAction(false, false, true, WIFI_WATCHDOG_FALLBACK_MS - 1,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// At/after the fallback window the watchdog opens SoftAP so the user is never
// locked out — this is the core resilience guarantee of PRO-333 criterion 2.
void test_down_at_fallback_opens_softap(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::OPEN_SOFTAP),
                          static_cast<int>(wifiWatchdogAction(false, false, true, WIFI_WATCHDOG_FALLBACK_MS,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

void test_down_well_past_fallback_opens_softap(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::OPEN_SOFTAP),
                          static_cast<int>(wifiWatchdogAction(false, false, true, WIFI_WATCHDOG_FALLBACK_MS * 4,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// PRO-333 re-arm gap (review P2): the sticky-isApConnection bug was that once a
// SoftAP fallback set apMode=true, the watchdog self-disabled forever. The
// Controller fix re-arms the watchdog on ARDUINO_EVENT_WIFI_STA_GOT_IP by
// clearing isApConnection (apMode->false) and resetting the down-clock
// (downForMs->0). These tests pin that the *policy* itself does not encode a
// permanent disable: it is purely a function of the current apMode/downForMs
// the Controller feeds it, so the exact re-arm transition the reviewer flagged
// (initial-connect timeout -> SoftAP -> later STA association) is recoverable.

// While in AP mode the policy is inert no matter how long the STA has been
// "down" — the SoftAP is the reachable surface, so there is nothing to recover.
// This is what makes the fallback stable; recovery comes from STA_GOT_IP
// flipping apMode back to false (modeled by the next test), NOT from the policy.
void test_ap_mode_is_noop_regardless_of_down_time(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(false, /*apMode*/ true, true, WIFI_WATCHDOG_FALLBACK_MS * 10,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// The full sticky-isApConnection scenario, step by step, at the pure-policy
// level. After the Controller re-arms (apMode=false, downForMs=0 from the
// STA_GOT_IP handler), the watchdog is fully live again: a fresh sustained STA
// loss progresses NONE -> RECONNECT -> OPEN_SOFTAP exactly as on first boot.
// Before the P2 fix the watchdog never re-evaluated at all once apMode latched;
// this asserts the decision contract that the Controller fix relies on.
void test_rearm_after_softap_then_sta_recovery(void) {
    // 1. Initial connect timed out, watchdog opened SoftAP: apMode latched true.
    //    Policy is inert while the SoftAP serves the user.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(false, /*apMode*/ true, true, WIFI_WATCHDOG_FALLBACK_MS * 2,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));

    // 2. The home network returns; STA associates and STA_GOT_IP fires. The
    //    Controller clears isApConnection and zeroes the down-clock. While the
    //    link is up the policy is NONE (re-armed but nothing to do).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(/*staConnected*/ true, /*apMode*/ false, true, 0,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));

    // 3. A LATER sustained STA loss (apMode now false again) is acted on just
    //    like the very first time: past grace -> RECONNECT, past fallback ->
    //    OPEN_SOFTAP. The watchdog is provably NOT permanently self-disabled.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::RECONNECT),
                          static_cast<int>(wifiWatchdogAction(false, /*apMode*/ false, true, WIFI_WATCHDOG_GRACE_MS,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::OPEN_SOFTAP),
                          static_cast<int>(wifiWatchdogAction(false, /*apMode*/ false, true, WIFI_WATCHDOG_FALLBACK_MS,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// The default thresholds must keep the invariant the static_assert pins: a
// device can never be stuck off-LAN and off-AP longer than the fallback window,
// and the reconnect attempt always precedes the AP fallback.
void test_default_thresholds_are_ordered(void) {
    TEST_ASSERT_TRUE(WIFI_WATCHDOG_FALLBACK_MS > WIFI_WATCHDOG_GRACE_MS);
    TEST_ASSERT_TRUE(WIFI_WATCHDOG_GRACE_MS > 0);
}

// PRO-365: staLinkUsable() decides whether the STA link is actually usable, not
// merely associated. A HomeKit/HomeSpan ASSOC_LEAVE -> AUTH_EXPIRE loop can
// leave the ESP32 in a half-open state where WiFi.status() still reports
// WL_CONNECTED (statusConnected=true) but the DHCP lease is gone
// (hasValidIp=false). The old watchdog judged liveness on status alone, so it
// kept zeroing the down-clock and the recovery path never fired. The link is
// usable only when BOTH the association AND a routable IP are present.

// Associated with a valid IP: the only genuinely usable state.
void test_link_usable_when_connected_with_ip(void) { TEST_ASSERT_TRUE(staLinkUsable(true, true)); }

// The PRO-365 bug shape: associated but no routable IP (half-open AUTH_EXPIRE).
// Must be treated as DOWN so the down-clock advances and recovery proceeds.
void test_link_down_when_connected_without_ip(void) { TEST_ASSERT_FALSE(staLinkUsable(true, false)); }

// Not associated: down regardless of any stale IP the netif might still report.
void test_link_down_when_not_connected(void) {
    TEST_ASSERT_FALSE(staLinkUsable(false, true));
    TEST_ASSERT_FALSE(staLinkUsable(false, false));
}

// PRO-365 end-to-end at the pure level: feed staLinkUsable() into the same
// staConnected slot wifiWatchdogAction() reads and confirm the half-open
// association (associated, no IP) now progresses NONE -> RECONNECT -> OPEN_SOFTAP
// exactly like a hard disconnect, instead of being frozen as "connected". This
// is the regression that pins the fix: before PRO-365, WiFi.status()==WL_CONNECTED
// alone reported the link up and the watchdog never acted.
void test_half_open_association_progresses_to_recovery(void) {
    // Half-open: associated but no routable IP -> the watchdog must see "down".
    const bool halfOpenConnected = staLinkUsable(/*statusConnected*/ true, /*hasValidIp*/ false);
    TEST_ASSERT_FALSE(halfOpenConnected);

    // Within grace: hold off (auto-reconnect gets a chance).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(halfOpenConnected, /*apMode*/ false, /*staConfigured*/ true,
                                                              WIFI_WATCHDOG_GRACE_MS - 1, WIFI_WATCHDOG_GRACE_MS,
                                                              WIFI_WATCHDOG_FALLBACK_MS)));
    // Past grace: re-assert STA.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::RECONNECT),
                          static_cast<int>(wifiWatchdogAction(halfOpenConnected, false, true, WIFI_WATCHDOG_GRACE_MS,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
    // Past fallback: open SoftAP so the user is never locked out.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::OPEN_SOFTAP),
                          static_cast<int>(wifiWatchdogAction(halfOpenConnected, false, true, WIFI_WATCHDOG_FALLBACK_MS,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

// A genuinely healthy link (associated + IP) still short-circuits to NONE even
// well past the fallback window — the fix must not make a good link look down.
void test_healthy_link_is_noop_past_fallback(void) {
    const bool healthyConnected = staLinkUsable(/*statusConnected*/ true, /*hasValidIp*/ true);
    TEST_ASSERT_TRUE(healthyConnected);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WiFiWatchdogAction::NONE),
                          static_cast<int>(wifiWatchdogAction(healthyConnected, false, true, WIFI_WATCHDOG_FALLBACK_MS * 4,
                                                              WIFI_WATCHDOG_GRACE_MS, WIFI_WATCHDOG_FALLBACK_MS)));
}

static int runWiFiFallbackPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_connected_is_noop);
    RUN_TEST(test_ap_mode_is_noop);
    RUN_TEST(test_unconfigured_is_noop);
    RUN_TEST(test_down_within_grace_is_noop);
    RUN_TEST(test_down_at_grace_reconnects);
    RUN_TEST(test_down_between_windows_reconnects);
    RUN_TEST(test_down_just_before_fallback_reconnects);
    RUN_TEST(test_down_at_fallback_opens_softap);
    RUN_TEST(test_down_well_past_fallback_opens_softap);
    RUN_TEST(test_ap_mode_is_noop_regardless_of_down_time);
    RUN_TEST(test_rearm_after_softap_then_sta_recovery);
    RUN_TEST(test_default_thresholds_are_ordered);
    RUN_TEST(test_link_usable_when_connected_with_ip);
    RUN_TEST(test_link_down_when_connected_without_ip);
    RUN_TEST(test_link_down_when_not_connected);
    RUN_TEST(test_half_open_association_progresses_to_recovery);
    RUN_TEST(test_healthy_link_is_noop_past_fallback);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runWiFiFallbackPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runWiFiFallbackPolicyTests(); }
#endif
