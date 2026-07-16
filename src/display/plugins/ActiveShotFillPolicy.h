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

enum class ActiveFillAdmission : uint8_t { Reject, Queue, Persist };

// startRecording() publishes the active identity before record() creates the
// .slog. A valid fill in that short interval is queued, then record() adopts it
// only if this same identity is still active after the log exists.
inline ActiveFillAdmission admitActiveFill(const ActiveShotIdentity &admitted, const ActiveShotIdentity &current,
                                           uint32_t requestedId, bool historyExists) {
    if (!isActiveFillFor(admitted, current, requestedId)) {
        return ActiveFillAdmission::Reject;
    }
    return historyExists ? ActiveFillAdmission::Persist : ActiveFillAdmission::Queue;
}

} // namespace shot_notes
