#ifndef OTA_UPDATING_POLICY_HPP
#define OTA_UPDATING_POLICY_HPP

// PRO-655 A-P2-1: lifecycle of BLE_OTA_DFU::updating, kept pure so it can be
// host-tested (test/test_ota_updating_policy). `updating` exempts the controller's
// ping watchdog from force-dropping the BLE link mid-DFU, so it must never latch:
// every exit path of a transfer clears it.

namespace ota_updating {

enum class Event {
    TransferCommand, // 0xFF setup / 0xFD start / 0xFC flash-write accepted
    TransferRejected, // malformed / invalid setup or write packet
    SizeMismatch,    // last part received but received != expected size
    InstallStarted,  // all parts received, install task kicked (device reboots after)
    InstallFailed,   // install task could not open/verify/apply update.bin
    InstallFinished, // install task done (success or Update error), about to reboot
    Disconnect,      // BLE peer disconnected (aborts any transfer)
};

// Returns the new `updating` value.
inline bool next(bool updating, Event e) {
    switch (e) {
    case Event::TransferCommand:
        return true;
    case Event::InstallStarted:
        // Install is in progress until the task reboots; keep the exemption so
        // the watchdog does not tear the link while the result is reported.
        return updating;
    case Event::TransferRejected:
    case Event::SizeMismatch:
    case Event::InstallFailed:
    case Event::InstallFinished:
    case Event::Disconnect:
        return false;
    }
    return false;
}

} // namespace ota_updating

#endif // OTA_UPDATING_POLICY_HPP
