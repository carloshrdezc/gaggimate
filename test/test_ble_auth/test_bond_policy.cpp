#include <unity.h>

#include "BondPolicy.h"

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
    RUN_TEST(test_recovery_no_retry_when_secure_succeeded);
    RUN_TEST(test_recovery_wipes_when_failed_with_stale_bond);
    RUN_TEST(test_recovery_does_not_loop_after_wiping_once);
    RUN_TEST(test_recovery_skips_when_no_local_bond_to_wipe);
    return UNITY_END();
}
