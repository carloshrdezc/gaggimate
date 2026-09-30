#ifndef DISPLAY_SYSTEM_INFO_H
#define DISPLAY_SYSTEM_INFO_H

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <vector>

// PRO-655: controller capabilities + identity as the display tracks them, populated
// from the SystemInfo the controller pushes over NanoPbComm. (Previously lived in
// lib/NimBLEComm/src/NimBLEComm.h.) Shape follows upstream/master (v6).
struct SystemCapabilities {
    bool dimming = false;
    bool pressure = false;
    bool ledControl = false;
    bool tof = false;
    bool dualBoiler = false; // v6; always false on Carlos hardware until PRO-657
    std::vector<uint32_t> addons;

    bool hasAddon(uint32_t addon) const { return std::find(addons.begin(), addons.end(), addon) != addons.end(); }
};

struct SystemInfo {
    String hardware;
    String version;
    SystemCapabilities capabilities;
    uint32_t protocolVersion = 0;  // controller's protocol version (0 = unknown / legacy)
    bool protocolMismatch = false; // set when it differs from the display's
};

#endif // DISPLAY_SYSTEM_INFO_H
