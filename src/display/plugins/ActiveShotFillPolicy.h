#pragma once

#include <stdint.h>

namespace shot_notes {

// A Dashboard fill captures this identity before waiting for notes I/O. The
// generation changes at every start/end transition, so rechecking it rejects a
// fill whose shot ended or was replaced while it waited without taking the
// recording state lock under notesMutex.
struct ActiveShotIdentity {
    uint32_t generation;
    uint32_t id;
};

inline bool isActiveFillFor(const ActiveShotIdentity &captured, const ActiveShotIdentity &current, uint32_t requestedId) {
    return captured.generation != 0 && captured.generation == current.generation && captured.id == requestedId &&
           current.id == requestedId;
}

} // namespace shot_notes
