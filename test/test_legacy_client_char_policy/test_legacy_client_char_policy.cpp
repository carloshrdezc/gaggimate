// PRO-669: host coverage for the legacy BLE client's required-characteristic
// guard (lib/NimBLEComm/src/LegacyClientCharPolicy.h).
#include <LegacyClientCharPolicy.h>
#include <unity.h>

static LegacyClientChars all() {
    LegacyClientChars c;
    c.outputControl = c.sensor = c.ping = c.info = true;
    return c;
}

void setUp() {}
void tearDown() {}

void test_all_present_is_compatible() {
    TEST_ASSERT_TRUE(legacyClientIsCompatible(all()));
    TEST_ASSERT_EQUAL_STRING("", legacyClientMissingRequiredChars(all()).c_str());
}

void test_each_missing_is_named() {
    LegacyClientChars c = all();
    c.outputControl = false;
    TEST_ASSERT_FALSE(legacyClientIsCompatible(c));
    TEST_ASSERT_EQUAL_STRING("outputControl", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.sensor = false;
    TEST_ASSERT_EQUAL_STRING("sensor", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.ping = false;
    TEST_ASSERT_EQUAL_STRING("ping", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.info = false;
    TEST_ASSERT_EQUAL_STRING("info", legacyClientMissingRequiredChars(c).c_str());
}

void test_none_present_lists_all() {
    TEST_ASSERT_EQUAL_STRING("outputControl, sensor, ping, info", legacyClientMissingRequiredChars(LegacyClientChars{}).c_str());
}

static constexpr uint64_t ADDR_A = 0x112233445566ULL;
static constexpr uint64_t ADDR_B = 0xAABBCCDDEEFFULL;
static constexpr uint32_t W = LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS;

void test_inactive_blocks_nothing() {
    LegacyClientBackoff b;
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_A, 0, 5));
}

void test_same_address_blocked_in_window() {
    LegacyClientBackoff b = legacyClientBackoffReject(ADDR_A, 0, 1000);
    TEST_ASSERT_TRUE(legacyClientAdvertBlocked(b, ADDR_A, 0, 1000));
    TEST_ASSERT_TRUE(legacyClientAdvertBlocked(b, ADDR_A, 0, 1000 + W - 1));
    TEST_ASSERT_TRUE(b.active);
}

void test_different_address_allowed_in_window() {
    LegacyClientBackoff b = legacyClientBackoffReject(ADDR_A, 0, 1000);
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_B, 0, 1001));
    // same 48-bit value, different address type => different device
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_A, 1, 1001));
    // allowing B must not lift A's block
    TEST_ASSERT_TRUE(legacyClientAdvertBlocked(b, ADDR_A, 0, 1002));
}

void test_expiry_clears_address() {
    LegacyClientBackoff b = legacyClientBackoffReject(ADDR_A, 0, 1000);
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_A, 0, 1000 + W));
    TEST_ASSERT_FALSE(b.active);
    TEST_ASSERT_EQUAL_UINT64(0, b.address);
}

void test_expiry_via_other_address_advert_clears() {
    LegacyClientBackoff b = legacyClientBackoffReject(ADDR_A, 0, 1000);
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_B, 0, 1000 + W));
    TEST_ASSERT_FALSE(b.active);
}

void test_millis_wraparound() {
    LegacyClientBackoff b = legacyClientBackoffReject(ADDR_A, 0, 0xFFFFFF00u);
    TEST_ASSERT_TRUE(legacyClientAdvertBlocked(b, ADDR_A, 0, 100)); // wrapped, 356 ms elapsed
    TEST_ASSERT_FALSE(legacyClientBackoffExpired(b, W - 0x101u));
    TEST_ASSERT_TRUE(legacyClientBackoffExpired(b, W - 0x100u));
    TEST_ASSERT_FALSE(legacyClientAdvertBlocked(b, ADDR_A, 0, W));
    TEST_ASSERT_FALSE(b.active);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_all_present_is_compatible);
    RUN_TEST(test_each_missing_is_named);
    RUN_TEST(test_none_present_lists_all);
    RUN_TEST(test_inactive_blocks_nothing);
    RUN_TEST(test_same_address_blocked_in_window);
    RUN_TEST(test_different_address_allowed_in_window);
    RUN_TEST(test_expiry_clears_address);
    RUN_TEST(test_expiry_via_other_address_advert_clears);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
