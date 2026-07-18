#include "display/core/MdnsNamePolicy.h"
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

void test_query_token_is_limited_to_explicit_log_download_routes(void) {
    const std::string token = "unit-test-token";

    TEST_ASSERT_TRUE(localAuthHttpRequestAuthenticated("", token, "GET", "/api/diag/log.txt", token));
    TEST_ASSERT_TRUE(localAuthHttpRequestAuthenticated("", token, "GET", "/api/diag/log.1", token));
    TEST_ASSERT_TRUE(localAuthHttpRequestAuthenticated("Bearer unit-test-token", "", "GET", "/api/diag/log.txt", token));
    TEST_ASSERT_FALSE(localAuthHttpRequestAuthenticated("", token, "GET", "/api/settings", token));
    TEST_ASSERT_FALSE(localAuthHttpRequestAuthenticated("", token, "POST", "/api/settings", token));
    TEST_ASSERT_FALSE(localAuthHttpRequestAuthenticated("", token, "GET", "/api/scales/list", token));
    TEST_ASSERT_FALSE(localAuthHttpRequestAuthenticated("", "wrong-token", "GET", "/api/diag/log.txt", token));
}

void test_ap_provisioning_requires_ap_mode_and_exact_required_fields(void) {
    TEST_ASSERT_TRUE(localAuthMayProvisionInAp(/*apMode=*/true, /*authenticated=*/true, /*hasSsid=*/true,
                                               /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/true, /*restart=*/true));
    TEST_ASSERT_FALSE(localAuthMayProvisionInAp(/*apMode=*/false, /*authenticated=*/true, /*hasSsid=*/true,
                                                /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/true, /*restart=*/true));
    TEST_ASSERT_FALSE(localAuthMayProvisionInAp(/*apMode=*/true, /*authenticated=*/false, /*hasSsid=*/true,
                                                /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/true, /*restart=*/true));
    TEST_ASSERT_FALSE(localAuthMayProvisionInAp(/*apMode=*/true, /*authenticated=*/true, /*hasSsid=*/false,
                                                /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/true, /*restart=*/true));
    TEST_ASSERT_FALSE(localAuthMayProvisionInAp(/*apMode=*/true, /*authenticated=*/true, /*hasSsid=*/true,
                                                /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/false, /*restart=*/true));
    TEST_ASSERT_FALSE(localAuthMayProvisionInAp(/*apMode=*/true, /*authenticated=*/true, /*hasSsid=*/true,
                                                /*hasPassword=*/true, /*hasMdnsName=*/true, /*complete=*/true, /*restart=*/false));
}

void test_saved_wifi_without_completed_provisioning_starts_recovery_ap(void) {
    TEST_ASSERT_TRUE(localAuthRequiresRecoveryAp(/*hasWifiCredentials=*/true, /*provisioned=*/false));
    TEST_ASSERT_FALSE(localAuthRequiresRecoveryAp(/*hasWifiCredentials=*/true, /*provisioned=*/true));
    TEST_ASSERT_FALSE(localAuthRequiresRecoveryAp(/*hasWifiCredentials=*/false, /*provisioned=*/false));
}

void test_websocket_requires_an_authenticated_session_for_commands(void) {
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/true, /*sessionAuthenticated=*/false, "req:ota-start"));
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/true, "req:ota-start"));
    TEST_ASSERT_TRUE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:auth"));
    TEST_ASSERT_FALSE(
        localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:process:activate"));
    TEST_ASSERT_FALSE(localAuthWebSocketMessageAllowed(/*isRelay=*/false, /*sessionAuthenticated=*/false, "req:profiles:list"));
}

void test_websocket_disconnect_does_not_preserve_authentication_for_a_reused_client_id(void) {
    std::unordered_map<uint32_t, bool> authenticatedClients{{42, true}};
    TEST_ASSERT_TRUE(localAuthWebSocketSessionAuthenticated(authenticatedClients, 42));

    authenticatedClients.erase(42);
    TEST_ASSERT_FALSE(localAuthWebSocketSessionAuthenticated(authenticatedClients, 42));

    authenticatedClients.emplace(42, false);
    TEST_ASSERT_FALSE(localAuthWebSocketSessionAuthenticated(authenticatedClients, 42));
}

void test_normal_operation_never_emits_wildcard_cors(void) {
    TEST_ASSERT_FALSE(localAuthShouldEmitCors(/*apMode=*/false, /*developmentBuild=*/false));
    TEST_ASSERT_FALSE(localAuthShouldEmitCors(/*apMode=*/true, /*developmentBuild=*/false));
    TEST_ASSERT_TRUE(localAuthShouldEmitCors(/*apMode=*/false, /*developmentBuild=*/true));
}

void test_mdns_name_policy_rejects_unsafe_persisted_values(void) {
    TEST_ASSERT_TRUE(isValidMdnsName("new-gaggimate"));
    TEST_ASSERT_TRUE(isValidMdnsName("gaggimate2"));
    TEST_ASSERT_TRUE(isValidMdnsName("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker.example/"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker?example"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker#example"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker@example"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker example"));
    TEST_ASSERT_FALSE(isValidMdnsName("-attacker"));
    TEST_ASSERT_FALSE(isValidMdnsName("attacker-"));
    TEST_ASSERT_FALSE(isValidMdnsName(""));
    TEST_ASSERT_FALSE(isValidMdnsName("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl"));
}

static int runLocalAuthPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_bearer_token_must_match_exactly);
    RUN_TEST(test_only_ap_setup_can_bypass_http_authentication);
    RUN_TEST(test_query_token_is_limited_to_explicit_log_download_routes);
    RUN_TEST(test_ap_provisioning_requires_ap_mode_and_exact_required_fields);
    RUN_TEST(test_saved_wifi_without_completed_provisioning_starts_recovery_ap);
    RUN_TEST(test_websocket_requires_an_authenticated_session_for_commands);
    RUN_TEST(test_websocket_disconnect_does_not_preserve_authentication_for_a_reused_client_id);
    RUN_TEST(test_normal_operation_never_emits_wildcard_cors);
    RUN_TEST(test_mdns_name_policy_rejects_unsafe_persisted_values);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runLocalAuthPolicyTests(); }
void loop() {}
#else
int main(int, char **) { return runLocalAuthPolicyTests(); }
#endif
