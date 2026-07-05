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

#ifndef GAGGIMATE_ENABLE_BLE_SCALE
#define GAGGIMATE_ENABLE_BLE_SCALE 1
#endif

// WebUI / relay-websocket integration. When disabled, the device builds as a
// headless/no-web configuration: there is no ESPAsyncWebServer, no `/api/*`
// HTTP routes, no `/ws` WebSocket, no captive portal, no cloud relay, and no
// OTA-over-web. WebUIPlugin owns ALL of that surface internally, so no other
// code holds a server pointer to dangle — gating its registration removes the
// feature cleanly (see CAR-383). This is independent of GAGGIMATE_HEADLESS,
// which only drops the physical display panel/UI: the four combinations
// (screen+web, screen-only, headless+web, headless+no-web) are all valid.
#ifndef GAGGIMATE_ENABLE_WEBUI
#define GAGGIMATE_ENABLE_WEBUI 1
#endif

// PRO-12: display-driver-family selection. Three hardware panel families ship
// in src/display/drivers/ (LilyGo, AMOLED, Waveshare). The default build
// (`pio run -e display`) compiles ALL three and keeps the full runtime
// autodetect chain in Controller::setupPanel() unchanged.
//
// A board-specific build can shrink its firmware footprint by compiling in only
// the family (or families) that board can actually use: a `-DGM_DRIVER_<FAMILY>`
// build flag, paired with a build_src_filter that excludes the other families'
// source dirs, drops the unused driver code from both the compile set and the
// selection chain (each isCompatible() branch + its #include is guarded by the
// matching macro below).
//
// Semantics: if NONE of the three macros is defined by the build, define all
// three (the default, autodetect-everything behavior — zero change vs. before).
// If a build defines at least one, only the defined families compile in. Guard
// board-specific envs must also narrow build_src_filter to physically exclude
// the excluded families' driver dirs (see platformio.ini display-<board> envs).
#if !defined(GM_DRIVER_LILYGO) && !defined(GM_DRIVER_AMOLED) && !defined(GM_DRIVER_WAVESHARE)
#define GM_DRIVER_LILYGO 1
#define GM_DRIVER_AMOLED 1
#define GM_DRIVER_WAVESHARE 1
#endif

#ifndef GM_DRIVER_LILYGO
#define GM_DRIVER_LILYGO 0
#endif

#ifndef GM_DRIVER_AMOLED
#define GM_DRIVER_AMOLED 0
#endif

#ifndef GM_DRIVER_WAVESHARE
#define GM_DRIVER_WAVESHARE 0
#endif

#endif // GAGGIMATE_FEATURES_H
