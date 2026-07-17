#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

inline bool localAuthBearerMatches(const std::string &authorization, const std::string &token) {
    static constexpr char kBearerPrefix[] = "Bearer ";
    return !token.empty() && authorization.size() == sizeof(kBearerPrefix) - 1 + token.size() &&
           authorization.compare(0, sizeof(kBearerPrefix) - 1, kBearerPrefix) == 0 &&
           authorization.compare(sizeof(kBearerPrefix) - 1, token.size(), token) == 0;
}

// AP mode is the physical-presence setup channel. Only the initial settings
// endpoint may use it without a token; all normal LAN operation is authenticated.
inline bool localAuthMayBypassHttpInSetup(bool apMode, bool bootstrapRoute) { return apMode && bootstrapRoute; }

// Cloud-relay requests are authenticated by the relay token before they enter this
// plugin. Local browser sessions must explicitly authenticate before any req:* work.
inline bool localAuthWebSocketMessageAllowed(bool isRelay, bool sessionAuthenticated, const std::string &messageType) {
    return isRelay || sessionAuthenticated || messageType == "req:auth";
}

inline bool localAuthWebSocketSessionAuthenticated(const std::unordered_map<uint32_t, bool> &authenticatedClients,
                                                   uint32_t clientId) {
    const auto session = authenticatedClients.find(clientId);
    return session != authenticatedClients.end() && session->second;
}

// Cross-origin browser access is disabled in firmware. A development build can
// opt in explicitly; AP setup stays same-origin and does not need CORS.
inline bool localAuthShouldEmitCors(bool /*apMode*/, bool developmentBuild) { return developmentBuild; }
