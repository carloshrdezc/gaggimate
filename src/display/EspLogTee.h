// PRO-273 — Route the display firmware's own ESP_LOG* output through the IDF
// esp_log_write() path so DiagnosticLogPlugin's esp_log_set_vprintf() tee
// actually captures it.
//
// THE BUG: on Arduino-ESP32 the build sets CONFIG_ARDUHAL_ESP_LOG (sdkconfig)
// but NOT USE_ESP_IDF_LOG, so framework header cores/esp32/esp32-hal-log.h
// expands ESP_LOGx(tag, ...) -> log_x(...) -> log_printf() -> log_printfv(),
// which calls ets_printf() and writes to UART DIRECTLY (verified in
// cores/esp32/esp32-hal-uart.c:818). The esp_log_set_vprintf() hook the tee
// installs is therefore NEVER invoked by any ESP_LOG* call — the tee installs,
// its proof-of-life packet broadcasts, but no log line is ever captured.
//
// WHY NOT just define -DUSE_ESP_IDF_LOG: that flag rewrites the *bare* log_x
// macros (log_i/log_e/...) to reference a `TAG` symbol that vendored libraries
// (SensorLib, TFT_eSPI, GFX) AND our own panel drivers
// (drivers/LilyGo-T-RGB/LilyGo_RGBPanel.cpp, drivers/Waveshare/WavesharePanel.cpp)
// don't define when they call `log_i("...")` directly -> "'TAG' was not declared
// in this scope" compile errors across the tree. Confirmed by building with the
// flag. So we must NOT touch the log_x macros at all.
//
// THE FIX (this header): force-included into src/ ONLY (platformio.ini
// build_src_flags = -include ...), so libraries are untouched. We pull in the
// real <Arduino.h> first (firmware TUs include it anyway), let esp32-hal-log.h
// define its ARDUHAL ESP_LOGx -> log_x macros, then REDEFINE only the five
// ESP_LOGx macros to call esp_log_write() directly. Every firmware/driver
// ESP_LOG* call site passes a const char* tag (string literal or .c_str()), so
// this override compiles cleanly. Bare log_x calls in the panel drivers are
// left on the untouched ARDUHAL path. esp_log_write() routes through the
// registered vprintf -> the tee, and (because the IDF default vprintf chained
// by the tee still writes to UART) serial-over-USB output is preserved. The
// shim reproduces the ARDUHAL line framing — timestamp, level letter,
// file:line, function, and tag (see GAGGIMATE_ESP_LOG below) — so serial lines
// keep the same shape/observability they had before, the only difference being
// the route (esp_log_write instead of ets_printf).
//
// The RUNTIME tag-level gate (initArduino() does esp_log_level_set("*", ERROR))
// is raised to INFO at boot in main.cpp so INFO+ reaches esp_log_write() and
// thus the tee (and UART), matching the levels CORE_DEBUG_LEVEL=3 already
// allowed. When diagnostics is OFF the tee is never installed, so ESP_LOG* just
// flows esp_log_write() -> default UART vprintf: same UART output as before, no
// hot-path tee cost.
#ifndef GAGGIMATE_ESP_LOG_TEE_H
#define GAGGIMATE_ESP_LOG_TEE_H

// Not applicable to the host simulator / native test builds: there ESP_LOG* is
// provided by the sim/native shims (sim/platform/esp_log.h, test/native/Arduino.h)
// and there is no IDF esp_log_write(). GAGGIMATE_SIM is set by [env:display-sim];
// the native test env never force-includes this header.
//
// C++ ONLY: the tag-coercion overload set below needs C++. The generated LVGL UI
// is C (src/display/ui/**/*.c) and this header is force-included into those TUs
// too — but they never emit ESP_LOG* (and any future C ESP_LOG would just stay on
// the ARDUHAL path). So we gate the whole override on __cplusplus and leave C TUs
// completely untouched.
#if !defined(GAGGIMATE_SIM) && defined(__cplusplus)

// Pull in the genuine framework logging header (via Arduino.h) FIRST so its
// ARDUHAL ESP_LOGx -> log_x definitions are in place; our redefinitions below
// then win for the rest of the translation unit.
#include <Arduino.h>
#include <esp_log.h>

// Some firmware tags are an Arduino String (e.g. Controller.cpp / AutoWakeupPlugin.cpp
// `const String LOG_TAG = F("...")`), others are a plain string literal. The old
// ARDUHAL path "worked" only because it passed the tag through C varargs (where a
// String object slid past %s as UB). esp_log_write() takes a typed `const char*`
// tag, so coerce both forms to const char* at the call site with this overload set.
static inline const char *gmLogTag(const char *tag) { return tag; }
static inline const char *gmLogTag(const String &tag) { return tag.c_str(); }

// Map an esp_log_level_t to the single ARDUHAL level letter (E/W/I/D/V) so the
// emitted serial/UDP/SD line keeps the level marker the old ARDUHAL framing had.
static inline char gmLogLevelLetter(esp_log_level_t level) {
    switch (level) {
    case ESP_LOG_ERROR:
        return 'E';
    case ESP_LOG_WARN:
        return 'W';
    case ESP_LOG_INFO:
        return 'I';
    case ESP_LOG_DEBUG:
        return 'D';
    case ESP_LOG_VERBOSE:
        return 'V';
    default:
        return '?';
    }
}

// esp_log.h leaves LOG_LOCAL_LEVEL at CONFIG_LOG_MAXIMUM_LEVEL (ERROR) because
// USE_ESP_IDF_LOG is not defined; that would compile out INFO/DEBUG from
// ESP_LOG_LEVEL_LOCAL. Force the local compile-time gate to INFO so INFO+ is
// emitted (CORE_DEBUG_LEVEL=3 == INFO; DEBUG/VERBOSE remain gated out, matching
// the prior ARDUHAL behaviour).
#undef GAGGIMATE_LOG_LOCAL_LEVEL
#define GAGGIMATE_LOG_LOCAL_LEVEL ESP_LOG_INFO

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV

// Route straight to esp_log_write() (which invokes the registered vprintf — the
// tee — and falls back to the default UART vprintf). The compile-time level
// check mirrors ESP_LOG_LEVEL_LOCAL but uses our forced INFO local level.
//
// Framing reproduces the ARDUHAL line shape so serial-over-USB (and the UDP/SD
// tee) keep their timestamp, level letter, and source location:
//   [<ms>][<L>][file:line] func(): [tag] message\r\n
// esp_log_timestamp() supplies the boot-relative millisecond timestamp,
// gmLogLevelLetter() the E/W/I/D/V marker, and __FILE__/__LINE__/__func__ the
// call-site location (all cheap macro/inline expansions, -Wformat clean). The
// shim owns the trailing "\r\n" line terminator — call sites must NOT append
// their own newline.
#define GAGGIMATE_ESP_LOG(level, tag, format, ...)                                                                               \
    do {                                                                                                                         \
        if (GAGGIMATE_LOG_LOCAL_LEVEL >= (level)) {                                                                              \
            const char *gmTag_ = gmLogTag(tag);                                                                                  \
            esp_log_write((level), gmTag_, "[%6lu][%c][%s:%u] %s(): [%s] " format "\r\n",                                        \
                          static_cast<unsigned long>(esp_log_timestamp()), gmLogLevelLetter(level), pathToFileName(__FILE__),    \
                          static_cast<unsigned>(__LINE__), __func__, gmTag_, ##__VA_ARGS__);                                     \
        }                                                                                                                        \
    } while (0)

#define ESP_LOGE(tag, format, ...) GAGGIMATE_ESP_LOG(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) GAGGIMATE_ESP_LOG(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) GAGGIMATE_ESP_LOG(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) GAGGIMATE_ESP_LOG(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) GAGGIMATE_ESP_LOG(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#endif // !GAGGIMATE_SIM

#endif // GAGGIMATE_ESP_LOG_TEE_H
