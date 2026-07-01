#ifndef WIFIFALLBACKPOLICY_H
#define WIFIFALLBACKPOLICY_H

// PRO-333: SoftAP-fallback / STA-recovery watchdog policy.
//
// Controller::setupWifi() only falls back to SoftAP when the *initial* connect
// loop fails. Once a station (STA) connection succeeds it is never re-checked,
// so when a previously-good STA link drops and does not come back (the
// HomeKit/HomeSpan ASSOC_LEAVE -> AUTH_EXPIRE loop is the motivating case, but a
// router reboot or moving out of range is the same shape) the device is
// stranded: not reachable on the LAN, and never on its own SoftAP either. The
// user is locked out of the web UI entirely.
//
// This header captures the pure decision the Controller's loop watchdog makes
// each tick, given only timing + link state. It deliberately has no Arduino /
// WiFi / FreeRTOS dependencies so it links and runs in [env:native] (the full
// Controller cannot be instantiated on the host).
//
// State machine the watchdog drives (Controller owns the side effects):
//   STA connected ............... nothing to do, remember "last seen up".
//   STA down, within grace ...... do nothing yet; autoReconnect may recover it.
//   STA down past grace ......... attempt an explicit STA reconnect (re-assert
//                                 Controller's ownership of the radio).
//   STA still down past fallback  open SoftAP so the user can always reach the
//                                 web UI / re-enter credentials.
//
// The two thresholds are separate on purpose: a short grace lets the normal
// Arduino auto-reconnect heal brief blips without churn, while the longer
// fallback window guarantees the device is never simultaneously off-LAN and
// off-AP for more than that bound.

enum class WiFiWatchdogAction {
    NONE,        // STA is up (or we are already in AP mode): take no action.
    RECONNECT,   // STA has been down past the grace window: re-assert STA.
    OPEN_SOFTAP, // STA has been down past the fallback window: open SoftAP.
};

// Decide what the watchdog should do this tick.
//
//   staConnected    - WiFi.status() == WL_CONNECTED right now.
//   apMode          - the device is currently serving its SoftAP (no STA to
//                     babysit; the user already has a reachable surface).
//   staConfigured   - WiFi credentials are configured (SSID + password set).
//                     With no credentials there is no STA to recover and the
//                     device is already meant to be in AP mode.
//   downForMs       - how long the STA has been continuously down (0 while up).
//   graceMs         - grace window before an explicit reconnect is attempted.
//   fallbackMs      - window before falling back to SoftAP. Must be > graceMs.
//
// Pure function: same inputs always yield the same action, no clock reads.
constexpr WiFiWatchdogAction wifiWatchdogAction(bool staConnected, bool apMode, bool staConfigured, unsigned long downForMs,
                                                unsigned long graceMs, unsigned long fallbackMs) {
    // Already connected, already in AP mode, or no STA configured at all: the
    // watchdog has nothing to recover.
    if (staConnected || apMode || !staConfigured) {
        return WiFiWatchdogAction::NONE;
    }
    // STA is down. Past the fallback window we must guarantee a reachable
    // surface, so open SoftAP. (>= so the boundary tick acts rather than waits.)
    if (downForMs >= fallbackMs) {
        return WiFiWatchdogAction::OPEN_SOFTAP;
    }
    // Past the grace window but before fallback: re-assert STA ownership.
    if (downForMs >= graceMs) {
        return WiFiWatchdogAction::RECONNECT;
    }
    // Within the grace window: let auto-reconnect try to heal the blip.
    return WiFiWatchdogAction::NONE;
}

// PRO-365: decide whether the STA link is actually USABLE, not merely
// "associated". The watchdog originally treated `WiFi.status() == WL_CONNECTED`
// as the sole liveness signal. During a HomeKit/HomeSpan ASSOC_LEAVE ->
// AUTH_EXPIRE loop (and some router-side deauth / half-open cases) the ESP32 STA
// can sit in a half-open association where WiFi.status() still reports
// WL_CONNECTED even though the link is unusable: the DHCP lease was dropped, so
// there is no routable IP and the device is unreachable. In that state the
// watchdog's `connected` branch kept zeroing the down-clock every tick, so the
// RECONNECT / OPEN_SOFTAP recovery path never fired and the device stayed
// stranded until a manual reboot (the reported PRO-365 symptom: ~78 min uptime,
// zero WiFi/reconnect log activity).
//
// The link is only usable when BOTH hold: the driver reports associated
// (statusConnected) AND we hold a valid, routable STA IP (hasValidIp; i.e.
// WiFi.localIP() != 0.0.0.0, the no-DHCP-lease sentinel). Treating "associated
// but no IP" as DOWN lets the down-clock advance and recovery proceed. Kept
// pure (no Arduino / WiFi headers) so [env:native] can cover it; the Controller
// computes the two booleans from WiFi.status() and WiFi.localIP() at the call
// site.
constexpr bool staLinkUsable(bool statusConnected, bool hasValidIp) { return statusConnected && hasValidIp; }

// Default thresholds used by Controller. Kept here so the firmware and the host
// test share one source of truth.
//   - 15s grace: long enough that a normal auto-reconnect (which retries on the
//     order of seconds) gets a fair chance before we intervene.
//   - 45s fallback: bounds the worst-case "locked out" window. After this the
//     user can always reach the SoftAP, even mid HomeKit ASSOC/AUTH loop.
constexpr unsigned long WIFI_WATCHDOG_GRACE_MS = 15000;
constexpr unsigned long WIFI_WATCHDOG_FALLBACK_MS = 45000;

static_assert(WIFI_WATCHDOG_FALLBACK_MS > WIFI_WATCHDOG_GRACE_MS,
              "PRO-333: SoftAP fallback window must be strictly longer than the reconnect grace window");

#endif // WIFIFALLBACKPOLICY_H
