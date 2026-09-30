#include "../../lib/ble_ota_dfu/src/ota_updating_policy.hpp"
#include <unity.h>

// PRO-655 A-P2-1: BLE_OTA_DFU::updating exempts the ping watchdog from dropping the
// link. It must clear on every exit path so the exemption can never stick.

using ota_updating::Event;
using ota_updating::next;

void setUp(void) {}
void tearDown(void) {}

void test_transfer_command_sets(void) {
    TEST_ASSERT_TRUE(next(false, Event::TransferCommand));
    TEST_ASSERT_TRUE(next(true, Event::TransferCommand));
}

void test_every_exit_path_clears(void) {
    const Event exits[] = {Event::TransferRejected, Event::SizeMismatch, Event::InstallFailed, Event::InstallFinished,
                           Event::Disconnect};
    for (Event e : exits) {
        TEST_ASSERT_FALSE(next(true, e));
        TEST_ASSERT_FALSE(next(false, e));
    }
}

void test_install_started_keeps_state(void) {
    TEST_ASSERT_TRUE(next(true, Event::InstallStarted));
    TEST_ASSERT_FALSE(next(false, Event::InstallStarted));
}

void test_aborted_transfer_then_disconnect_does_not_latch(void) {
    bool u = false;
    u = next(u, Event::TransferCommand); // 0xFF
    u = next(u, Event::TransferCommand); // 0xFD
    u = next(u, Event::TransferCommand); // 0xFC part 1
    TEST_ASSERT_TRUE(u);
    u = next(u, Event::Disconnect); // peer vanished mid-transfer
    TEST_ASSERT_FALSE(u);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_transfer_command_sets);
    RUN_TEST(test_every_exit_path_clears);
    RUN_TEST(test_install_started_keeps_state);
    RUN_TEST(test_aborted_transfer_then_disconnect_does_not_latch);
    return UNITY_END();
}
