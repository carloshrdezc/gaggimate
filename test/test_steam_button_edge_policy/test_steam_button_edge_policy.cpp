#include "../../src/display/core/SteamButtonPolicy.h"
#include <unity.h>

// PRO-391: Stop-Steam bounces back to Steam with a non-momentary (latching)
// steam switch. Pressing web-UI Stop-Steam shows Standby for an instant, then
// the machine bounces straight back to Steam; a second manual Standby is needed
// to make it stick.
//
// Root cause: a non-momentary switch reports a persistent LEVEL, not a one-shot
// press. steamButtonStatus stays 1 while the physical switch is closed. The old
// Controller::handleSteamButton() treated every level-high notification as a
// fresh press, so `case MODE_STANDBY: setMode(MODE_STEAM)` re-asserted Steam
// right after an explicit web-UI Standby fired while the switch was still ON.
//
// Fix: for NON-momentary switches, only ENTER_STEAM on the RISING EDGE of the
// level (previous low -> current high). A sustained latched-high level after an
// explicit Standby is not a fresh assertion, so Standby wins; a genuine re-press
// (toggle the switch off then on) is a real rising edge and still enters Steam.
// Momentary buttons are one-shot presses at the source and are intentionally
// NOT edge-gated — they keep behaving exactly as before.
//
// The full Controller cannot be instantiated on the host ([env:native] does not
// shim BLE/LVGL/FreeRTOS), so this pins the pure edge-vs-level decision that
// SteamButtonPolicy.h captures and Controller::handleSteamButton() dispatches on.

void setUp(void) {}
void tearDown(void) {}

// --- Acceptance case 1: the regression this issue is about ------------------
// Non-momentary, mode == MODE_STANDBY, sustained level-high (status == 1 seen
// again after the switch was already high) must NOT re-enter Steam. This models
// web-UI Stop-Steam (-> STANDBY) firing while the physical switch is latched ON.
void test_nonmomentary_standby_sustained_high_stays_standby(void) {
    // previousLevel == 1 (already latched high), currentLevel == 1 (still high).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/1, /*cur=*/1, MODE_STANDBY)));
}

// A repeated sustained-high notification from BREW likewise must not re-assert
// Steam — only the edge counts.
void test_nonmomentary_brew_sustained_high_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/1, /*cur=*/1, MODE_BREW)));
}

// --- Acceptance case 2: normal latching toggle preserved --------------------
// Non-momentary, mode == MODE_BREW, rising edge (0 then 1) => enter Steam.
void test_nonmomentary_brew_rising_edge_enters_steam(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::ENTER_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/0, /*cur=*/1, MODE_BREW)));
}

// A genuine re-press after landing in Standby (switch toggled off then on) is a
// rising edge and must still be able to enter Steam — the switch never wedges.
void test_nonmomentary_standby_rising_edge_enters_steam(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::ENTER_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/0, /*cur=*/1, MODE_STANDBY)));
}

// --- Acceptance case 3: release path preserved ------------------------------
// Non-momentary, mode == MODE_STEAM, level goes low (status == 0) => deactivate
// + return to Brew.
void test_nonmomentary_steam_level_low_exits_to_brew(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::EXIT_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/1, /*cur=*/0, MODE_STEAM)));
}

// Level low outside STEAM (e.g. already in Standby) is a no-op for a
// non-momentary switch.
void test_nonmomentary_standby_level_low_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/1, /*cur=*/0, MODE_STANDBY)));
}

// --- Acceptance case 4: momentary behavior unchanged ------------------------
// Momentary press enters Steam from STANDBY and from BREW exactly as before,
// regardless of the previous level (momentary is one-shot at the source and is
// NOT edge-gated).
void test_momentary_press_enters_steam_from_standby(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::ENTER_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/true, /*prev=*/0, /*cur=*/1, MODE_STANDBY)));
    // Even if the framework re-delivers prev==1, a momentary press still fires.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::ENTER_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/true, /*prev=*/1, /*cur=*/1, MODE_STANDBY)));
}

void test_momentary_press_enters_steam_from_brew(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::ENTER_STEAM),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/true, /*prev=*/0, /*cur=*/1, MODE_BREW)));
}

// Momentary release (level low) is a no-op — the pre-PRO-391 handler only ran
// the exit path for non-momentary switches.
void test_momentary_level_low_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/true, /*prev=*/1, /*cur=*/0, MODE_STEAM)));
}

// Steam-mode sustained press does not re-enter (already in Steam) and a press
// while in an unrelated mode (WATER) is a no-op.
void test_press_in_unrelated_mode_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/false, /*prev=*/0, /*cur=*/1, MODE_WATER)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SteamButtonAction::NONE),
                          static_cast<int>(decideSteamButtonAction(/*momentary=*/true, /*prev=*/0, /*cur=*/1, MODE_WATER)));
}

static int runSteamButtonEdgePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_nonmomentary_standby_sustained_high_stays_standby);
    RUN_TEST(test_nonmomentary_brew_sustained_high_is_noop);
    RUN_TEST(test_nonmomentary_brew_rising_edge_enters_steam);
    RUN_TEST(test_nonmomentary_standby_rising_edge_enters_steam);
    RUN_TEST(test_nonmomentary_steam_level_low_exits_to_brew);
    RUN_TEST(test_nonmomentary_standby_level_low_is_noop);
    RUN_TEST(test_momentary_press_enters_steam_from_standby);
    RUN_TEST(test_momentary_press_enters_steam_from_brew);
    RUN_TEST(test_momentary_level_low_is_noop);
    RUN_TEST(test_press_in_unrelated_mode_is_noop);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runSteamButtonEdgePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runSteamButtonEdgePolicyTests(); }
#endif
