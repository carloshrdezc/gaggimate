#include <unity.h>

#include "BondPolicy.h"

static gm_ble_addr_t addr(uint8_t last) {
    gm_ble_addr_t a{};
    a.type = 0;
    a.val[0] = 0xAA;
    a.val[1] = 0xBB;
    a.val[2] = 0xCC;
    a.val[3] = 0xDD;
    a.val[4] = 0xEE;
    a.val[5] = last;
    return a;
}

void test_no_bonds_returns_false(void) {
    auto connecting = addr(0x01);
    TEST_ASSERT_FALSE(shouldWipeBondsBeforePair(0, nullptr, &connecting));
}

void test_matching_bond_returns_false(void) {
    auto stored = addr(0x01);
    auto connecting = addr(0x01);
    TEST_ASSERT_FALSE(shouldWipeBondsBeforePair(1, &stored, &connecting));
}

void test_mismatched_bond_returns_true(void) {
    auto stored = addr(0x01);
    auto connecting = addr(0x02);
    TEST_ASSERT_TRUE(shouldWipeBondsBeforePair(1, &stored, &connecting));
}

void test_bond_storage_exhausted_returns_true(void) {
    auto stored = addr(0x01);
    auto connecting = addr(0x02);
    TEST_ASSERT_TRUE(shouldWipeBondsBeforePair(BOND_POLICY_MAX_BONDS, &stored, &connecting));
}

void test_null_stored_with_nonzero_count_returns_true(void) {
    auto connecting = addr(0x01);
    TEST_ASSERT_TRUE(shouldWipeBondsBeforePair(1, nullptr, &connecting));
}

// --- Client-side recovery: shouldWipeLocalBondsAndRetry ---
// After client->secureConnection() fails, decide whether to wipe the local
// bonds and retry a fresh pair. Must wipe at most once per connect cycle (loop
// guard) and only when there is actually a stale local bond to clear.

void test_recovery_no_retry_when_secure_succeeded(void) {
    // Encryption established: nothing to recover.
    TEST_ASSERT_FALSE(shouldWipeLocalBondsAndRetry(/*secureSucceeded*/ true, /*alreadyWiped*/ false, /*localBondCount*/ 1));
}

void test_recovery_wipes_when_failed_with_stale_bond(void) {
    // Failed with a stale local bond present and not yet wiped: recover.
    TEST_ASSERT_TRUE(shouldWipeLocalBondsAndRetry(/*secureSucceeded*/ false, /*alreadyWiped*/ false, /*localBondCount*/ 1));
}

void test_recovery_does_not_loop_after_wiping_once(void) {
    // Already wiped this cycle: do not retry again (prevents infinite wipe loop).
    TEST_ASSERT_FALSE(shouldWipeLocalBondsAndRetry(/*secureSucceeded*/ false, /*alreadyWiped*/ true, /*localBondCount*/ 1));
}

void test_recovery_skips_when_no_local_bond_to_wipe(void) {
    // Failure with zero local bonds: wiping would change nothing, so don't mask
    // the real failure with a pointless retry.
    TEST_ASSERT_FALSE(shouldWipeLocalBondsAndRetry(/*secureSucceeded*/ false, /*alreadyWiped*/ false, /*localBondCount*/ 0));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_bonds_returns_false);
    RUN_TEST(test_matching_bond_returns_false);
    RUN_TEST(test_mismatched_bond_returns_true);
    RUN_TEST(test_bond_storage_exhausted_returns_true);
    RUN_TEST(test_null_stored_with_nonzero_count_returns_true);
    RUN_TEST(test_recovery_no_retry_when_secure_succeeded);
    RUN_TEST(test_recovery_wipes_when_failed_with_stale_bond);
    RUN_TEST(test_recovery_does_not_loop_after_wiping_once);
    RUN_TEST(test_recovery_skips_when_no_local_bond_to_wipe);
    return UNITY_END();
}
