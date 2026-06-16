#ifndef GAGGIMATE_FEATURES_H
#define GAGGIMATE_FEATURES_H

// Central compile-time feature-flag header.
//
// Each optional firmware feature gets a GAGGIMATE_ENABLE_<FEATURE> macro that
// defaults to 1 (enabled). The default build (`pio run -e display`) therefore
// behaves exactly as before — these flags only become meaningful when a build
// overrides one via a `-D` build flag (e.g. `-DGAGGIMATE_ENABLE_HOMEKIT=0`).
//
// Pattern for adding a sibling flag (MQTT, BLE-scale, WebUI, ...):
//
//     #ifndef GAGGIMATE_ENABLE_<FEATURE>
//     #define GAGGIMATE_ENABLE_<FEATURE> 1
//     #endif
//
// Guard the feature's `#include` and its registration in Controller.cpp with
// `#if GAGGIMATE_ENABLE_<FEATURE>`. When a feature is disabled, make sure any
// behavior that other code relies on (e.g. network discoverability) still has
// a sensible fallback.

#ifndef GAGGIMATE_ENABLE_HOMEKIT
#define GAGGIMATE_ENABLE_HOMEKIT 1
#endif

#ifndef GAGGIMATE_ENABLE_MQTT
#define GAGGIMATE_ENABLE_MQTT 1
#endif

#endif // GAGGIMATE_FEATURES_H
