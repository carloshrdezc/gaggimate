// Minimal Arduino.h shim for HOST (native) unit-test builds only.
//
// This header is ONLY on the include path for the PlatformIO [env:native]
// test environment (see platformio.ini `build_flags = -I test/native`). It is
// never seen by real firmware builds, which use the genuine ESP32/Arduino
// headers from the espressif32 platform.
//
// Goal: let pure firmware LOGIC (profile parsing, the event system, small
// value types) compile and run on a normal desktop compiler without dragging
// in FreeRTOS, NimBLE, LVGL or the ESP-IDF. We provide just enough of the
// Arduino `String` API plus no-op logging macros for the code under test.
//
// Keep this SMALL. If a test needs more of the Arduino surface, prefer
// seaming the production code (thin interface / test double) over growing
// this shim into a second Arduino implementation.
#ifndef GAGGIMATE_NATIVE_ARDUINO_SHIM_H
#define GAGGIMATE_NATIVE_ARDUINO_SHIM_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// String: a thin std::string wrapper that mirrors the subset of Arduino's
// WString API exercised by the firmware logic under test.
//
// [env:native] defines ARDUINOJSON_ENABLE_ARDUINO_STRING=1 so ArduinoJson 7
// installs its Converter<::String> (powering obj["x"].as<String>() /
// is<String>()) and its Writer<::String>. That writer resets the destination
// with `str = (const char *)0` and appends through `concat(...)`, so this shim
// implements operator=(const char *) and concat(...) to match.
// ---------------------------------------------------------------------------
class String {
  public:
    String() = default;
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}
    explicit String(float v) : _s(std::to_string(v)) {}
    explicit String(double v) : _s(std::to_string(v)) {}

    const char *c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }

    char operator[](size_t i) const { return _s[i]; }
    char &operator[](size_t i) { return _s[i]; }

    String &operator+=(const String &o) {
        _s += o._s;
        return *this;
    }
    String &operator+=(const char *o) {
        _s += (o ? o : "");
        return *this;
    }

    // Arduino's WString assigns from a (possibly null) C string; ArduinoJson's
    // Writer<String> resets the destination with `str = (const char *)0` before
    // writing. Mirror that null-safe assignment here.
    String &operator=(const char *o) {
        _s = (o ? o : "");
        return *this;
    }

    // Arduino's WString::concat returns 1 on success, 0 on allocation failure.
    // ArduinoJson 7.4's Writer<String>::flush() relies on this (`if
    // (destination_->concat(buffer_)) ...`). std::string growth throws rather
    // than returning a failure code, so success is unconditional here.
    unsigned char concat(const char *o) {
        _s += (o ? o : "");
        return 1;
    }
    unsigned char concat(const String &o) {
        _s += o._s;
        return 1;
    }
    unsigned char concat(char c) {
        _s += c;
        return 1;
    }

    bool operator==(const String &o) const { return _s == o._s; }
    bool operator!=(const String &o) const { return _s != o._s; }
    bool operator==(const char *o) const { return _s == (o ? o : ""); }
    bool operator!=(const char *o) const { return _s != (o ? o : ""); }
    bool operator<(const String &o) const { return _s < o._s; }

    const std::string &std_str() const { return _s; }

  private:
    std::string _s;
};

inline String operator+(const String &a, const String &b) { return String(a.std_str() + b.std_str()); }
inline String operator+(const String &a, const char *b) { return String(a.std_str() + (b ? b : "")); }
inline String operator+(const char *a, const String &b) { return String(std::string(a ? a : "") + b.std_str()); }
inline bool operator==(const char *a, const String &b) { return b == a; }
inline bool operator!=(const char *a, const String &b) { return b != a; }

// ---------------------------------------------------------------------------
// Logging macros: no-ops on host. Firmware uses ESP_LOG* freely; on host we
// drop them so test output stays clean and we don't depend on the ESP-IDF.
// ---------------------------------------------------------------------------
#ifndef ESP_LOGV
#define ESP_LOGV(tag, ...) ((void)0)
#endif
#ifndef ESP_LOGD
#define ESP_LOGD(tag, ...) ((void)0)
#endif
#ifndef ESP_LOGI
#define ESP_LOGI(tag, ...) ((void)0)
#endif
#ifndef ESP_LOGW
#define ESP_LOGW(tag, ...) ((void)0)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, ...) ((void)0)
#endif

#endif // GAGGIMATE_NATIVE_ARDUINO_SHIM_H
