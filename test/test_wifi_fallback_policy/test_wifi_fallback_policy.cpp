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

// The default thresholds must keep the invariant the static_assert pins: a
// device can never be stuck off-LAN and off-AP longer than the fallback window,
// and the reconnect attempt always precedes the AP fallback.
void test_default_thresholds_are_ordered(void) {
    TEST_ASSERT_TRUE(WIFI_WATCHDOG_FALLBACK_MS > WIFI_WATCHDOG_GRACE_MS);
    TEST_ASSERT_TRUE(WIFI_WATCHDOG_GRACE_MS > 0);
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
    RUN_TEST(test_default_thresholds_are_ordered);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runWiFiFallbackPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runWiFiFallbackPolicyTests(); }
#endif
