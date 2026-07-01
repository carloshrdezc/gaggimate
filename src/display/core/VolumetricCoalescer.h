#ifndef VOLUMETRICCOALESCER_H
#define VOLUMETRICCOALESCER_H

// PRO-367: the pure, hardware-independent kernel of the "coalesce-latest on a
// failed mutex take" behavior used by the volumetric stop path. Split out so it
// can be unit-tested on the host (test/native) WITHOUT dragging in FreeRTOS.
//
// The problem it solves: the stop-critical volumetric update takes a mutex with
// a short (10 ms) fail-fast timeout. Under diagnostic-log contention that take
// can fail, and the old code SILENTLY DROPPED the measurement — so the running
// BrewProcess's currentVolume never advanced toward the target and the yield
// stop never fired.
//
// Bluetooth/scale weight is monotonic cumulative, so the freshest value already
// subsumes every value that came before it. That means we never need to queue a
// history — we only need to remember the LATEST value and apply it on the next
// successful take. This is exactly a coalesce-latest: a failed take latches the
// value; the next successful take consumes the freshest latched value and clears
// the pending flag.
//
// A single-writer/single-reader scalar handoff on the ESP32 is fine with the
// values living behind the same mutex on the consume side; the latch write on
// the failed-take side is the only unlocked touch and is intentionally cheap.

#include <cstddef>

namespace volumetric {

// Coalesce-latest holder for a monotonic-cumulative measurement.
//
// Usage on the hot path (per source):
//   - On a FAILED mutex take: coalescer.latch(measurement); return;  // never drop
//   - On a SUCCESSFUL mutex take: latch the current measurement too, then
//     `consume()` the freshest pending value (if any) and apply it once. Because
//     the value is cumulative, applying the freshest latched value is correct and
//     loses no yield.
struct Coalescer {
    double latched = 0.0;
    bool pending = false;

    // Record the freshest measurement. Overwrites any previously-latched value
    // (older cumulative reads are subsumed by the newest one). Cheap enough to
    // run outside the lock on the caller's task.
    void latch(double measurement) {
        latched = measurement;
        pending = true;
    }

    bool hasPending() const { return pending; }

    // Return the freshest latched value and clear the pending flag. Callers must
    // check hasPending() first (or use consumeInto). Idempotent-safe: a second
    // call with nothing pending returns the last latched value but reports it via
    // the return of consumeInto().
    double take() {
        pending = false;
        return latched;
    }

    // Convenience: if a value is pending, write it into `out` and clear pending,
    // returning true. Otherwise leave `out` untouched and return false. This is
    // the shape the successful-take branch uses: "apply the freshest value iff
    // one is pending".
    bool consumeInto(double &out) {
        if (!pending)
            return false;
        out = latched;
        pending = false;
        return true;
    }
};

} // namespace volumetric

#endif // VOLUMETRICCOALESCER_H
