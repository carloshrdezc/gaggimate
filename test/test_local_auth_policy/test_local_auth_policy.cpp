#include "display/plugins/LocalAuthPolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_bearer_token_must_match_exactly(void) {
    TEST_ASSERT_TRUE(localAuthBearerMatches("Bearer unit-test-token", "unit-test-token"));
    TEST_ASSERT_FALSE(localAuthBearerMatches("Bearer wrong", "unit-test-token"));
    TEST_ASSERT_FALSE(localAuthBearerMatches("Basic unit-test-token", "unit-test-token"));
    TEST_ASSERT_FALSE(localAuthBearerMatches("Bearer unit-test-token ", "unit-test-token"));
    TEST_ASSERT_FALSE(localAuthBearerMatches("Bearer unit-test-token", ""));
}

void test_only_ap_setup_can_bypass_http_authentication(void) {
    TEST_ASSERT_TRUE(localAuthMayBypassHttpInSetup(/*apMode=*/true, /*bootstrapRoute=*/true));
    TEST_ASSERT_FALSE(localAuthMayBypassHttpInSetup(/*apMode=*/false, /*bootstrapRoute=*/true));
    TEST_ASSERT_FALSE(localAuthMayBypassHttpInSetup(/*apMode=*/true, /*bootstrapRoute=*/false));
}

void test_websocket_requires_an_authenticated_session_for_commands(void) {
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/true, /*sessionAuthenticated=*/false, "req:ota-start"));
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/true, "req:ota-start"));
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:auth"));
    TEST_ASSERT_FALSE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:process:activate"));
    TEST_ASSERT_FALSE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:profiles:list"));
}

void test_normal_operation_never_emits_wildcard_cors(void) {
    TEST_ASSERT_FALSE(localAuthShouldEmitCors(/*apMode=*/false, /*developmentBuild=*/false));
    TEST_ASSERT_FALSE(localAuthShouldEmitCors(/*apMode=*/true, /*developmentBuild=*/false));
    TEST_ASSERT_TRUE(localAuthShouldEmitCors(/*apMode=*/false, /*developmentBuild=*/true));
}

static int runLocalAuthPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_bearer_token_must_match_exactly);
    RUN_TEST(test_only_ap_setup_can_bypass_http_authentication);
    RUN_TEST(test_websocket_requires_an_authenticated_session_for_commands);
    RUN_TEST(test_normal_operation_never_emits_wildcard_cors);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runLocalAuthPolicyTests(); }
void loop() {}
#else
int main(int, char **) { return runLocalAuthPolicyTests(); }
#endif
