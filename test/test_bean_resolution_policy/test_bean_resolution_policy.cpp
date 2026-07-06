#include "../../src/display/plugins/BeanResolutionPolicy.h"
#include <unity.h>

// PRO-422: the shot record durably carries the bean it was pulled with as a
// (beanId, beanName) pair. Resolving that record back to a live bean must prefer
// the stable id and only fall back to a normalized name match when no id was
// recorded. This shares one source of truth (BeanResolutionPolicy.h) with the
// firmware findBeanIndexForNotes() and the capture-time name->id lookup.

using bean_resolution::BeanRef;
using bean_resolution::resolveBeanIndex;

void setUp(void) {}
void tearDown(void) {}

static std::vector<BeanRef> sampleBeans() {
    return {
        {"id-ethiopia", "Ethiopia Yirgacheffe"},
        {"id-colombia", "Colombia Huila"},
        {"id-dupe", "House Blend"},
    };
}

void test_resolves_by_recorded_id_first(void) {
    const auto beans = sampleBeans();
    // Even if the recorded NAME points elsewhere, a matching id wins.
    TEST_ASSERT_EQUAL_INT(1, resolveBeanIndex("id-colombia", "Ethiopia Yirgacheffe", beans));
}

void test_id_match_is_independent_of_name(void) {
    const auto beans = sampleBeans();
    TEST_ASSERT_EQUAL_INT(0, resolveBeanIndex("id-ethiopia", "", beans));
}

void test_falls_back_to_name_when_no_id_recorded(void) {
    const auto beans = sampleBeans();
    TEST_ASSERT_EQUAL_INT(1, resolveBeanIndex("", "Colombia Huila", beans));
}

void test_name_match_is_normalized_case_and_whitespace(void) {
    const auto beans = sampleBeans();
    TEST_ASSERT_EQUAL_INT(0, resolveBeanIndex("", "  ETHIOPIA YIRGACHEFFE  ", beans));
}

void test_falls_back_to_name_when_recorded_id_missing(void) {
    const auto beans = sampleBeans();
    // A recorded id that no longer matches any bean (e.g. bean recreated on
    // another device) falls through to the name — preserving pre-PRO-422
    // bean-usage accounting behavior.
    TEST_ASSERT_EQUAL_INT(2, resolveBeanIndex("stale-id", "House Blend", beans));
}

void test_returns_minus_one_when_nothing_matches(void) {
    const auto beans = sampleBeans();
    TEST_ASSERT_EQUAL_INT(-1, resolveBeanIndex("unknown-id", "Unknown Bean", beans));
}

void test_empty_id_and_empty_name_is_no_match(void) {
    const auto beans = sampleBeans();
    TEST_ASSERT_EQUAL_INT(-1, resolveBeanIndex("", "", beans));
    TEST_ASSERT_EQUAL_INT(-1, resolveBeanIndex("", "   ", beans));
}

static int runBeanResolutionPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_resolves_by_recorded_id_first);
    RUN_TEST(test_id_match_is_independent_of_name);
    RUN_TEST(test_falls_back_to_name_when_no_id_recorded);
    RUN_TEST(test_name_match_is_normalized_case_and_whitespace);
    RUN_TEST(test_falls_back_to_name_when_recorded_id_missing);
    RUN_TEST(test_returns_minus_one_when_nothing_matches);
    RUN_TEST(test_empty_id_and_empty_name_is_no_match);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBeanResolutionPolicyTests(); }
void loop() {}
#else
int main() { return runBeanResolutionPolicyTests(); }
#endif
