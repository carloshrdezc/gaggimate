# BLE Pairing & Recovery

The display and controller communicate over an authenticated, encrypted BLE link. They pair automatically on first boot and
reconnect transparently across reboots. You should not see a pairing prompt during normal use.

## First boot

Power on both devices. The display will connect to the controller and complete pairing within about 5 seconds. After this,
brew, steam, grind, and OTA updates work normally.

## Recovering from a failed link

If the web UI shows "BLE pairing failed - factory reset required", the two devices' bonds have drifted, typically because
one was reflashed or factory-reset alone. Recovery is a coordinated re-pair:

1. Connect a serial console to the controller at 115200 baud and send the character `B`. The controller will log
   `Factory-resetting BLE bonds`.
2. Connect a serial console to the display and send `B`. The display will log the same.
3. Power-cycle both devices.
4. They will pair fresh on next boot, and the warning will clear.

## What this protects against

- An attacker in BLE range cannot send the OTA `0xEF` flash-format command, drive the boiler, valve, or pump, change PID
  settings, or trigger autotune. Every write requires an authenticated, encrypted link.
- The one-time pairing window on first boot is the only opportunity for a man-in-the-middle attack. Pair in a trusted
  environment.

## Limits

- Bonds live in NVS. A full firmware re-flash that wipes NVS will require a coordinated re-pair.
- The controller has no UI for passkey display, so Just Works pairing is used. MITM-resistant pairing is not available on
  this hardware.
