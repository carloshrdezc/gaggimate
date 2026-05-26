#ifndef MQTT_TOPIC_BUILDER_H
#define MQTT_TOPIC_BUILDER_H

#include <string>

namespace mqtt_topic {

inline std::string buildHomeAssistantDiscoveryTopic(const char *haTopic, const char *mac) {
    return std::string(haTopic) + "/device/" + mac + "/config";
}

inline std::string buildGaggimateTopic(const char *mac, const std::string &topic) {
    return std::string("gaggimate/") + mac + "/" + topic;
}

} // namespace mqtt_topic

#endif // MQTT_TOPIC_BUILDER_H
