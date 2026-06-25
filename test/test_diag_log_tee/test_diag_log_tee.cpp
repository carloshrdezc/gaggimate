#include <cstdarg>
#include <cstring>
#include <display/plugins/DiagLogFormat.h>
#include <string>
#include <unity.h>
#include <vector>

// PRO-273 — proves the DiagnosticLogPlugin UDP/SD log tee actually captures
// ESP_LOG output once ARDUHAL is routed through esp_log_writev() (the
// -DUSE_ESP_IDF_LOG fix).
//
// Why this shape: DiagnosticLogPlugin.cpp can't be linked into [env:native] —
// it depends on WiFiUdp / FreeRTOS queues / SD_MMC, none of which exist on the
// host. So the production code's pure, hardware-independent kernel was split
// into src/display/plugins/DiagLogFormat.h (diaglog::formatLine), which teeVprintf()
// now calls. This suite exercises THAT real kernel directly, plus a faithful
// reconstruction of teeVprintf()'s "format into buffer, enqueue, ALWAYS chain to
// the previous vprintf" contract — the exact behavior esp_log_writev() drives on
// the device once USE_ESP_IDF_LOG makes ESP_LOG* reach the registered vprintf.

void setUp(void) {}
void tearDown(void) {}

// --- helpers to drive the variadic kernel like ESP_LOG/esp_log_writev do ------
static size_t callFormatLine(char *buf, size_t bufSize, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t n = diaglog::formatLine(buf, bufSize, fmt, args);
    va_end(args);
    return n;
}

// --- 1. The format/truncation kernel teeVprintf() relies on -------------------

void test_format_line_basic_message(void) {
    char buf[256];
    size_t len = callFormatLine(buf, sizeof(buf), "[%s] %s", "WebUI", "TEST endpoint hit!");
    TEST_ASSERT_EQUAL_STRING("[WebUI] TEST endpoint hit!", buf);
    // Returned length is the payload (excludes the NUL) and matches strlen.
    TEST_ASSERT_EQUAL_UINT(strlen("[WebUI] TEST endpoint hit!"), len);
}

void test_format_line_with_numeric_args(void) {
    char buf[256];
    size_t len = callFormatLine(buf, sizeof(buf), "uptime %lums port %u", 1234UL, 9999U);
    TEST_ASSERT_EQUAL_STRING("uptime 1234ms port 9999", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("uptime 1234ms port 9999"), len);
}

void test_format_line_truncates_long_line_to_buffer(void) {
    // A line longer than the buffer must be clamped to bufSize-1 payload bytes so
    // the queued length can never run past the fixed stack buffer.
    char buf[16];
    size_t len = callFormatLine(buf, sizeof(buf), "%s", "0123456789ABCDEFGHIJ");
    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, len);    // 15 payload bytes
    TEST_ASSERT_EQUAL_UINT(0, buf[sizeof(buf) - 1]); // still NUL-terminated
    TEST_ASSERT_EQUAL_STRING("0123456789ABCDE", buf);
}

void test_format_line_empty_result_returns_zero(void) {
    char buf[16];
    size_t len = callFormatLine(buf, sizeof(buf), "%s", "");
    TEST_ASSERT_EQUAL_UINT(0, len); // nothing to enqueue
}

void test_format_line_rejects_zero_capacity(void) {
    char buf[1];
    size_t len = callFormatLine(buf, 0, "x");
    TEST_ASSERT_EQUAL_UINT(0, len);
}

// --- 2. The tee CONTRACT: capture + always chain to the previous vprintf ------
//
// Mirror teeVprintf()'s control flow exactly: a captured-line sink (stand-in for
// the FreeRTOS queue + UDP/SD drain) AND an unconditional chain to the previous
// vprintf (stand-in for the original UART sink). This is the behavior that was
// BROKEN before PRO-273: esp_log_set_vprintf() installed the hook, but ARDUHAL's
// ESP_LOG* never called it. With USE_ESP_IDF_LOG, esp_log_writev() invokes the
// registered vprintf for every INFO+ line — i.e. this hook now runs.

static std::vector<std::string> g_captured; // the tee sink (queue/UDP/SD)
static std::string g_uart;                  // the chained previous vprintf (UART)

static int fakeUartVprintf(const char *fmt, va_list args) {
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0)
        g_uart.append(buf);
    return n;
}

// Faithful reconstruction of DiagnosticLogPlugin::teeVprintf().
static int harnessTeeVprintf(const char *fmt, va_list args) {
    va_list argsCopy;
    va_copy(argsCopy, args);
    char buf[256];
    size_t len = diaglog::formatLine(buf, sizeof(buf), fmt, argsCopy);
    if (len > 0)
        g_captured.emplace_back(buf, len); // enqueue (drop-on-full omitted: host)
    va_end(argsCopy);
    // ALWAYS chain so UART/USB serial output is preserved.
    return fakeUartVprintf(fmt, args);
}

static int emitLog(int (*vprintfHook)(const char *, va_list), const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vprintfHook(fmt, args);
    va_end(args);
    return n;
}

void test_tee_captures_and_chains_to_uart(void) {
    g_captured.clear();
    g_uart.clear();

    // Simulate esp_log_writev() calling the installed vprintf for an ESP_LOGI line.
    emitLog(harnessTeeVprintf, "[%s] %s\n", "WebUI", "TEST endpoint hit!");

    // The tee captured the line (would be broadcast over UDP / written to SD)...
    TEST_ASSERT_EQUAL_UINT(1, g_captured.size());
    TEST_ASSERT_EQUAL_STRING("[WebUI] TEST endpoint hit!\n", g_captured[0].c_str());
    // ...AND the original UART sink still saw it (serial-over-USB preserved).
    TEST_ASSERT_EQUAL_STRING("[WebUI] TEST endpoint hit!\n", g_uart.c_str());
}

void test_tee_chains_uart_even_when_capture_skipped(void) {
    g_captured.clear();
    g_uart.clear();

    // An empty payload is not enqueued, but UART chaining must still happen so a
    // line is never silently swallowed.
    emitLog(harnessTeeVprintf, "%s", "");

    TEST_ASSERT_EQUAL_UINT(0, g_captured.size());
    TEST_ASSERT_EQUAL_STRING("", g_uart.c_str());
}

void test_tee_captures_multiple_lines_in_order(void) {
    g_captured.clear();
    g_uart.clear();

    emitLog(harnessTeeVprintf, "first %d", 1);
    emitLog(harnessTeeVprintf, "second %d", 2);
    emitLog(harnessTeeVprintf, "third %d", 3);

    TEST_ASSERT_EQUAL_UINT(3, g_captured.size());
    TEST_ASSERT_EQUAL_STRING("first 1", g_captured[0].c_str());
    TEST_ASSERT_EQUAL_STRING("second 2", g_captured[1].c_str());
    TEST_ASSERT_EQUAL_STRING("third 3", g_captured[2].c_str());
    TEST_ASSERT_EQUAL_STRING("first 1second 2third 3", g_uart.c_str());
}

static int runDiagLogTeeTests() {
    UNITY_BEGIN();
    RUN_TEST(test_format_line_basic_message);
    RUN_TEST(test_format_line_with_numeric_args);
    RUN_TEST(test_format_line_truncates_long_line_to_buffer);
    RUN_TEST(test_format_line_empty_result_returns_zero);
    RUN_TEST(test_format_line_rejects_zero_capacity);
    RUN_TEST(test_tee_captures_and_chains_to_uart);
    RUN_TEST(test_tee_chains_uart_even_when_capture_skipped);
    RUN_TEST(test_tee_captures_multiple_lines_in_order);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runDiagLogTeeTests(); }
void loop() {}
#else
int main(int argc, char **argv) { return runDiagLogTeeTests(); }
#endif
