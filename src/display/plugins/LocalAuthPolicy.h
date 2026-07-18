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

// Browser download anchors cannot set Authorization headers. Keep their query
// credential exception to the two explicit diagnostic-log GET routes only.
inline bool localAuthMayUseQueryToken(const std::string &method, const std::string &path) {
    return method == "GET" && (path == "/api/diag/log.txt" || path == "/api/diag/log.1");
}

inline bool localAuthHttpRequestAuthenticated(const std::string &authorization, const std::string &queryToken,
                                              const std::string &method, const std::string &path, const std::string &token) {
    if (localAuthBearerMatches(authorization, token))
        return true;
    return localAuthMayUseQueryToken(method, path) && localAuthBearerMatches("Bearer " + queryToken, token);
}

// AP mode is the physical-presence setup channel. Only the initial settings
// endpoint may use it without a token; all normal LAN operation is authenticated.
inline bool localAuthMayBypassHttpInSetup(bool apMode, bool bootstrapRoute) { return apMode && bootstrapRoute; }

// The AP handoff endpoint must remain a narrow, authenticated recovery action.
// It never delegates to the general settings mutation, whose checkbox fields
// intentionally treat omission as disabled.
inline bool localAuthMayProvisionInAp(bool apMode, bool authenticated, bool hasSsid, bool hasPassword, bool hasMdnsName,
                                      bool complete, bool restart) {
    return apMode && authenticated && hasSsid && hasPassword && hasMdnsName && complete && restart;
}

// Existing STA installations predate local credentials. Keep the recovery AP
// available until an owner completes the AP setup flow and persists the marker.
inline bool localAuthRequiresRecoveryAp(bool hasWifiCredentials, bool provisioned) { return hasWifiCredentials && !provisioned; }

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
