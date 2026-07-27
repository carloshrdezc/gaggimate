#ifndef RELAYCONNECTIONPOLICY_H
#define RELAYCONNECTIONPOLICY_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

// PRO-596: pure, host-testable extraction of the cloud-relay connection-parameter
// derivation previously inlined as the file-static free functions `parseRelayUrl`
// and `relayTokenProtocol` (plus the connect-path ternary) in WebUIPlugin.cpp.
//
// WebUIPlugin.cpp is NOT in the [env:native] build_src_filter, so before this
// extraction none of this URL-parsing / token-encoding logic had any host test
// coverage. It concentrated the HTTP server, WebSocket dispatch, OTA
// orchestration, and the relay bridge in one 2,835-line file; this carves out
// the relay bridge's stateless "turn settings into connection parameters"
// slice, mirroring the PRO-11 precedent (OtaIntentState.h / OtaChannelSwitchPolicy.h).
//
// This header holds ONLY deterministic value logic: no FreeRTOS, no WebSocket
// client, no Arduino `String`, no `Settings`. Every function is a pure function
// of its arguments. WebUIPlugin.cpp keeps the task lifecycle, the mutexes, the
// `relayWs` object and the `Settings` glue; it calls these functions with plain
// `std::string`/`const char*` values (adapting to/from Arduino `String` at the
// call boundary) and acts on the returned values. This is a pure refactor:
// every function reproduces the exact pre-existing inline behavior, just made
// host-compilable — no semantic change.

namespace relay_connection_policy {

// ---------------------------------------------------------------------------
// URL parsing: mirrors the inline `parseRelayUrl` in WebUIPlugin.cpp.
//
// Accepts only ws:// and wss:// URLs. On success `valid` is true and the
// remaining fields describe how to open the WebSocket:
//   * useSSL   — true for wss://, false for ws://
//   * host     — the host portion (no scheme, no port, no path)
//   * port     — explicit port if present, else the scheme default (443 wss,
//                80 ws). Parsed with Arduino String::toInt() semantics below.
//   * basePath — everything from the first '/' after the host onwards, or "/"
//                when the URL has no path component.
// Any URL not starting with a recognized scheme returns valid=false and leaves
// the other fields unspecified (callers must check `valid` first), exactly as
// the original returned `false` without writing the out-params meaningfully.
// ---------------------------------------------------------------------------

struct RelayUrlParts {
    bool valid;
    bool useSSL;
    std::string host;
    uint16_t port;
    std::string basePath;
};

// Reproduce Arduino String::toInt() for the port substring. On the ESP32
// Arduino core String::toInt() delegates to `atol(buffer())`, i.e.
// `strtol(buffer(), nullptr, 10)`, so its documented base-10 parse is:
//   1. skip leading whitespace (per isspace()),
//   2. an optional leading '+'/'-' sign,
//   3. a run of decimal digits, stopping at the first non-digit,
//   4. yield 0 when no digits follow (empty, all-whitespace, or non-numeric),
//   5. clamp to LONG_MIN/LONG_MAX on overflow (strtol saturates rather than
//      wrapping — this also avoids the signed-overflow UB of a hand-rolled
//      `value * 10 + digit` accumulator for arbitrarily long digit runs).
// We implement it directly in terms of std::strtol on the substring to match
// those semantics exactly. The original inline code assigned this long result
// to a uint16_t, so callers keep the identical (uint16_t) truncation.
inline long relayArduinoToInt(const std::string &s) { return std::strtol(s.c_str(), nullptr, 10); }

inline RelayUrlParts parseRelayUrl(const std::string &url) {
    RelayUrlParts parts{false, false, std::string(), 0, std::string()};

    auto splitAfterScheme = [&](std::size_t schemeLen, bool useSSL, uint16_t defaultPort) {
        parts.valid = true;
        parts.useSSL = useSSL;
        const std::string rest = url.substr(schemeLen);
        const std::size_t slashIdx = rest.find('/');
        const std::string hostPort = (slashIdx == std::string::npos) ? rest : rest.substr(0, slashIdx);
        parts.basePath = (slashIdx == std::string::npos) ? std::string("/") : rest.substr(slashIdx);
        const std::size_t colonIdx = hostPort.find(':');
        if (colonIdx == std::string::npos) {
            parts.host = hostPort;
            parts.port = defaultPort;
        } else {
            parts.host = hostPort.substr(0, colonIdx);
            parts.port = static_cast<uint16_t>(relayArduinoToInt(hostPort.substr(colonIdx + 1)));
        }
    };

    if (url.rfind("wss://", 0) == 0) {
        splitAfterScheme(6, /*useSSL=*/true, /*defaultPort=*/443);
        return parts;
    }
    if (url.rfind("ws://", 0) == 0) {
        splitAfterScheme(5, /*useSSL=*/false, /*defaultPort=*/80);
        return parts;
    }
    return parts; // valid == false
}

// ---------------------------------------------------------------------------
// Connect path: mirrors the inline ternary in startRelay():
//   String path = (basePath.isEmpty() || basePath == "/")
//                     ? "/connect?role=device"
//                     : basePath + "/connect?role=device";
// An empty or root base path uses the absolute connect path; any other base
// path is prefixed to it.
// ---------------------------------------------------------------------------

inline std::string resolveRelayConnectPath(const std::string &basePath) {
    if (basePath.empty() || basePath == "/") {
        return "/connect?role=device";
    }
    return basePath + "/connect?role=device";
}

// ---------------------------------------------------------------------------
// Token protocol header value: mirrors the inline `relayTokenProtocol`, a
// URL-safe base64 (RFC 4648 §5 alphabet, '-'/'_' for 62/63) of the raw token
// bytes WITHOUT '=' padding, prefixed with "gaggimate-token-". This becomes the
// second Sec-WebSocket-Protocol offer so the relay can authenticate the device.
//
// Preserved exactly: 3-byte groups pack big-endian into a 24-bit block; the
// 3rd/4th output chars are emitted only when the 2nd/3rd input bytes exist, so
// a trailing 1- or 2-byte group yields 2 or 3 chars (no padding), and the
// missing input bytes are treated as 0 when forming the block.
// ---------------------------------------------------------------------------

inline std::string relayTokenProtocol(const std::string &token) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    encoded.reserve(((token.length() + 2) / 3) * 4);
    for (std::size_t index = 0; index < token.length(); index += 3) {
        const uint32_t first = static_cast<uint8_t>(token[index]);
        const uint32_t second = index + 1 < token.length() ? static_cast<uint8_t>(token[index + 1]) : 0;
        const uint32_t third = index + 2 < token.length() ? static_cast<uint8_t>(token[index + 2]) : 0;
        const uint32_t block = (first << 16) | (second << 8) | third;
        encoded += alphabet[(block >> 18) & 0x3f];
        encoded += alphabet[(block >> 12) & 0x3f];
        if (index + 1 < token.length())
            encoded += alphabet[(block >> 6) & 0x3f];
        if (index + 2 < token.length())
            encoded += alphabet[block & 0x3f];
    }
    return "gaggimate-token-" + encoded;
}

} // namespace relay_connection_policy

#endif // RELAYCONNECTIONPOLICY_H
