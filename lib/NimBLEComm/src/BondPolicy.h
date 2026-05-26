#ifndef GAGGIMATE_BOND_POLICY_H
#define GAGGIMATE_BOND_POLICY_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO
// NimBLEDevice.h is the public umbrella header and is always on the include path
// for firmware builds; it transitively defines ble_addr_t via nimble/ble.h. (The
// raw <host/ble_hs.h> path is internal to NimBLE and not exposed to this library.)
#include <NimBLEDevice.h>
using gm_ble_addr_t = ble_addr_t;
#else
struct gm_ble_addr_t {
    uint8_t type;
    uint8_t val[6];
};
#endif

static constexpr size_t BOND_POLICY_MAX_BONDS = 3;

inline bool shouldWipeBondsBeforePair(size_t numBonds, const gm_ble_addr_t *storedPeerAddr,
                                      const gm_ble_addr_t *connectingPeerAddr) {
    if (numBonds == 0) {
        return false;
    }
    if (numBonds >= BOND_POLICY_MAX_BONDS) {
        return true;
    }
    if (storedPeerAddr == nullptr || connectingPeerAddr == nullptr) {
        return true;
    }
    if (storedPeerAddr->type != connectingPeerAddr->type) {
        return true;
    }
    return std::memcmp(storedPeerAddr->val, connectingPeerAddr->val, sizeof(storedPeerAddr->val)) != 0;
}

// Client-side recovery decision. After client->secureConnection() fails, decide
// whether to wipe the local bonds and retry a single fresh pair. A stale local
// LTK (e.g. left in NVS after re-flashing one board) makes encryption resumption
// fail silently; clearing it forces a clean re-pair. Returns true only when:
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
