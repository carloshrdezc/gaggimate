#ifndef DIAGLOGFORMAT_H
#define DIAGLOGFORMAT_H

// PRO-273: the pure, hardware-independent kernel of the DiagnosticLogPlugin
// vprintf tee, split out so it can be unit-tested on the host (test/native)
// WITHOUT dragging in WiFi/FreeRTOS/SD_MMC (which is why DiagnosticLogPlugin.cpp
// itself can't be compiled into [env:native]). teeVprintf() calls this to format
// one log line into its fixed stack buffer; the test exercises exactly this
// formatting + truncation contract.

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace diaglog {

// Format `format`/`args` into `buf` (capacity `bufSize`, must be >= 1) and return
// the number of payload bytes to enqueue (always < bufSize, i.e. excluding the
// terminating NUL). Returns 0 when nothing should be enqueued (encoding error or
// empty result). vsnprintf always NUL-terminates within the buffer; when the
// formatted line would exceed the buffer it is truncated to bufSize-1 bytes so
// the queued length never runs past the buffer. `args` is consumed.
inline size_t formatLine(char *buf, size_t bufSize, const char *format, va_list args) {
    if (buf == nullptr || bufSize == 0)
        return 0;
    int n = vsnprintf(buf, bufSize, format, args);
    if (n <= 0)
        return 0;
    return (static_cast<size_t>(n) < bufSize) ? static_cast<size_t>(n) : bufSize - 1;
}

} // namespace diaglog

#endif // DIAGLOGFORMAT_H
