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

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_bonds_returns_false);
    RUN_TEST(test_matching_bond_returns_false);
    RUN_TEST(test_mismatched_bond_returns_true);
    RUN_TEST(test_bond_storage_exhausted_returns_true);
    RUN_TEST(test_null_stored_with_nonzero_count_returns_true);
    return UNITY_END();
}
