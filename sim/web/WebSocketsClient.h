// Simulator shim for the links2004/WebSockets `WebSocketsClient` (used by
// WebUIPlugin's cloud-relay client, CAR-259). The desktop simulator has no
// cloud relay backend, so this is a no-op stand-in: the relay simply never
// connects. It provides exactly the API surface WebUIPlugin touches
// (onEvent/begin/beginSSL/sendTXT/loop/disconnect/setReconnectInterval) plus the
// WStype_t event enum, so the firmware compiles unchanged under GAGGIMATE_SIM.
#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <functional>

// Event types reported by the real library; the relay handler switches on these.
enum WStype_t {
    WStype_ERROR = 0,
    WStype_DISCONNECTED,
    WStype_CONNECTED,
    WStype_TEXT,
    WStype_BIN,
    WStype_FRAGMENT_TEXT_START,
    WStype_FRAGMENT_BIN_START,
    WStype_FRAGMENT,
    WStype_FRAGMENT_FIN,
    WStype_PING,
    WStype_PONG,
};

class WebSocketsClient {
  public:
    using WebSocketClientEvent = std::function<void(WStype_t type, uint8_t *payload, size_t length)>;

    // The relay never connects in the sim; these are all inert.
    void onEvent(WebSocketClientEvent cb) { _cb = std::move(cb); }
    void begin(const char * /*host*/, uint16_t /*port*/, const char * /*path*/ = "/") {}
    void beginSSL(const char * /*host*/, uint16_t /*port*/, const char * /*path*/ = "/") {}
    bool sendTXT(const String & /*payload*/) { return false; }
    bool sendTXT(const char * /*payload*/) { return false; }
    void loop() {}
    void disconnect() {}
    bool isConnected() { return false; }
    void setReconnectInterval(unsigned long /*ms*/) {}
    void enableHeartbeat(uint16_t /*pingInterval*/, uint16_t /*pongTimeout*/, uint8_t /*disconnectTimeoutCount*/) {}

  private:
    WebSocketClientEvent _cb;
};
