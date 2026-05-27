#ifndef GAGGIMATE_BOND_POLICY_H
#define GAGGIMATE_BOND_POLICY_H

#include <cstddef>

// Client-side recovery decision. After client->secureConnection() fails, decide
// whether to wipe the local bonds and retry a single fresh pair. A stale local
// LTK (e.g. left in NVS after re-flashing one board) makes encryption resumption
// fail silently; clearing it forces a clean re-pair, after which the controller's
// own NimBLE BLE_GAP_EVENT_REPEAT_PAIRING handler clears the matching stale peer.
// Returns true only when:
//   - encryption was NOT established, AND
//   - we have not already wiped during this connect cycle (prevents an infinite
//     wipe/retry loop against a peer that simply isn't pairable), AND
//   - there is at least one local bond to clear (otherwise wiping changes nothing
//     and a retry would only mask the real failure).
inline bool shouldWipeLocalBondsAndRetry(bool secureConnectionSucceeded, bool alreadyWipedThisCycle, size_t localBondCount) {
    if (secureConnectionSucceeded) {
        return false;
    }
    if (alreadyWipedThisCycle) {
        return false;
    }
    return localBondCount > 0;
}

#endif // GAGGIMATE_BOND_POLICY_H
