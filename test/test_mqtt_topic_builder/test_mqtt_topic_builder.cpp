#include <string>
#include <unity.h>

#include <display/plugins/MQTTTopicBuilder.h>

void test_build_home_assistant_discovery_topic_keeps_long_topic_intact(void) {
    const std::string haTopic = "gaggimate/this/is/a/very/long/home/assistant/topic/that/used/to/be/truncated/before/the/config/topic/segment";
    const char *mac = "AA_BB_CC_DD_EE_FF";

    const std::string topic = mqtt_topic::buildHomeAssistantDiscoveryTopic(haTopic.c_str(), mac);
    TEST_ASSERT_EQUAL_STRING((haTopic + "/device/AA_BB_CC_DD_EE_FF/config").c_str(), topic.c_str());
    TEST_ASSERT_TRUE(topic.size() > 80);
}

void test_build_gaggimate_topic_keeps_long_message_topic_intact(void) {
    const char *mac = "AA_BB_CC_DD_EE_FF";
    const std::string payloadTopic = "controller/telemetry/state/with/a/path/longer/than/the/old/fixed/buffer/limit";

    const std::string topic = mqtt_topic::buildGaggimateTopic(mac, payloadTopic);
    TEST_ASSERT_EQUAL_STRING((std::string("gaggimate/") + mac + "/" + payloadTopic).c_str(), topic.c_str());
    TEST_ASSERT_TRUE(topic.size() > 80);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_build_home_assistant_discovery_topic_keeps_long_topic_intact);
    RUN_TEST(test_build_gaggimate_topic_keeps_long_message_topic_intact);
    return UNITY_END();
}
