#ifndef UTILITIES_H
#define UTILITIES_H
#include "ControllerConfig.h"
#include <Arduino.h>

#include "comms.pb.h"
#include "pb_encode.h"

// PRO-243: encode the INFO characteristic as a nanopb SystemInfo message (was an
// ArduinoJson {"hw","v","cp":{"dm","ps","led","tof"}} string). Returns the raw
// protobuf bytes in an Arduino String (length-delimited, NUL-safe) so the
// existing initServer(String)/setInfo(String) signatures stay unchanged.
inline String make_system_info(ControllerConfig config, String version) {
    gaggimate_SystemInfo msg = gaggimate_SystemInfo_init_zero;
    snprintf(msg.hardware, sizeof(msg.hardware), "%s", config.name.c_str());
    snprintf(msg.version, sizeof(msg.version), "%s", version.c_str());
    msg.has_capabilities = true;
    msg.capabilities.dimming = config.capabilites.dimming;
    msg.capabilities.pressure = config.capabilites.pressure;
    msg.capabilities.led_control = config.capabilites.ledControls;
    msg.capabilities.tof = config.capabilites.tof;

    uint8_t buf[gaggimate_SystemInfo_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&os, gaggimate_SystemInfo_fields, &msg)) {
        // PRO-310: surface the encode failure instead of silently returning an
        // empty String (which the display decodes as an all-default SystemInfo,
        // bypassing Controller::setupInfos()'s decode-fail fallback).
        ESP_LOGE("make_system_info", "encode failed: %s", PB_GET_ERROR(&os));
        return String();
    }
    String out;
    out.reserve(os.bytes_written);
    for (size_t i = 0; i < os.bytes_written; i++) {
        out += static_cast<char>(buf[i]);
    }
    return out;
}

#endif // UTILITIES_H
