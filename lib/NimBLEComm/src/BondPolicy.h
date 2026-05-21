#ifndef GAGGIMATE_BOND_POLICY_H
#define GAGGIMATE_BOND_POLICY_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO
#include <host/ble_hs.h>
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

#endif // GAGGIMATE_BOND_POLICY_H
