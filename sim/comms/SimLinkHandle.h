// Sim stand-in for the native link handle that lib/NanoPbComm's
// GaggiMateClient::getClient() returns on device. The firmware never names that
// type: it only calls getClient()->getRssi() (WebUIPlugin status) and hands the
// pointer to GitHubOTA::init(), so the sim gets its own type instead of
// re-declaring a BLE-stack class name (PRO-656).
#pragma once

class SimLinkHandle {
  public:
    bool isConnected() const { return true; }
    int getRssi() const { return -50; }
};
