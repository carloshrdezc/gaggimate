#ifndef SHOTINDEXMETADATAPOLICY_H
#define SHOTINDEXMETADATAPOLICY_H

#include <display/models/shot_log_format.h>

// PRO-277: Pure field-merge rule applied by ShotHistoryPlugin::updateIndexMetadata()
// when a post-shot notes save updates an existing index entry in place.
//
// This is the contract the (now indexMutex-serialized) metadata update relies on once
// it has located the entry. Extracted as a free function so it can be unit-tested on
// the host without the Arduino File / SD seam:
//
//  - rating is always overwritten with the value from the saved notes.
//  - volume is overwritten only when a positive doseOut override was supplied
//    (volume > 0); a 0 means "no override" and must NOT clobber the recorded
//    final weight from the shot header.
//  - SHOT_FLAG_HAS_NOTES is set (never cleared) when a rating is present; the
//    other flags (COMPLETED / DELETED) are preserved untouched.
//
// Returns the entry with the merge applied; the caller writes it back at the same
// position. Keeping this branchless-and-pure makes the "metadata lands correctly"
// half of PRO-277 testable independently of the concurrency fix.
constexpr ShotIndexEntry applyIndexMetadata(ShotIndexEntry entry, uint8_t rating, uint16_t volume) {
    entry.rating = rating;
    if (volume > 0) {
        entry.volume = volume;
    }
    if (rating > 0) {
        entry.flags = static_cast<uint8_t>(entry.flags | SHOT_FLAG_HAS_NOTES);
    }
    return entry;
}

#endif // SHOTINDEXMETADATAPOLICY_H
