#ifndef OTACHANNELSWITCHPOLICY_H
#define OTACHANNELSWITCHPOLICY_H

#include <cstddef>

// PRO-400 (design: PRO-394 §1,§3,§4,§5a,§6-Issue-A): decision predicate for the
// WebUIPlugin OTA-start flash gate.
//
// Today the flash decision runs through the upgrade-only guard
// update_required(new, current) (lib/OTA/src/common.cpp: `_new_version >
// _current_version`). Only the `tag:<semver>` pinned path escapes it via
// force=true. A channel switch (stable <-> beta <-> nightly) persists the new
// `oc` and re-points the release URL but still hits the upgrade-only gate, so a
// downgrade-direction switch (beta -> stable, nightly -> stable) stalls
// silently even though the user explicitly asked for a different channel's head.
//
// This policy decides, from pure inputs, whether the flash should be:
//   - UpgradeOnly:        run the normal upgrade-only guard (semver must advance)
//   - ForceMatchTag:      pinned `tag:` path, resolved version confirmed == tag
//   - ForceChannelSwitch: channel changed and the new channel head resolved OK;
//                         force-flash the resolved head regardless of semver
//                         direction (moving tags / downgrade switch)
//   - Refuse:             cannot confirm what we would flash (tag mismatch, or a
//                         switch whose new-channel resolve failed / came back
//                         empty) — never flash something we can't confirm.
//
// Header-only + free of any Arduino-String method (no startsWith/substring):
// the caller passes primitives + `const char*` version strings, so this links
// on [env:native] via the existing `-I src` with the host String shim (which
// implements neither startsWith nor substring). Mirrors the ChangeModeDeferPolicy.h
// / PostStopGracePolicy.h header-only precedent in this directory. No new OTA
// .cpp is added to the native allow-list and `lib_ignore = OTA` stays untouched.

enum class OtaFlashDecision { UpgradeOnly, ForceMatchTag, ForceChannelSwitch, Refuse };

// Leading-`v`-tolerant version equality, covering both directions, mirroring the
// inline check at WebUIPlugin.cpp (`resolved == pinned || (pinned v-stripped) ||
// (resolved v-stripped)`). Pure `const char*` so it is identical on host and
// device. Null is treated as empty; two empty strings are considered equal here
// (callers gate emptiness separately where "resolve produced nothing" matters).
constexpr const char *otaStripLeadingV(const char *s) { return (s != nullptr && s[0] == 'v') ? s + 1 : s; }

// constexpr C-string equality (gnu++17 std::strcmp is not constexpr). Null is
// treated as empty.
constexpr bool otaCStrEq(const char *a, const char *b) {
    const char *aa = a ? a : "";
    const char *bb = b ? b : "";
    while (*aa != '\0' && *aa == *bb) {
        ++aa;
        ++bb;
    }
    return *aa == *bb;
}

// Leading-`v`-tolerant version equality, covering both directions.
constexpr bool otaVersionsEqualVTolerant(const char *a, const char *b) {
    return otaCStrEq(a, b) || otaCStrEq(otaStripLeadingV(a ? a : ""), b ? b : "") ||
           otaCStrEq(a ? a : "", otaStripLeadingV(b ? b : ""));
}

constexpr bool otaStrEmpty(const char *s) { return s == nullptr || s[0] == '\0'; }

// Core decision.
//
// Inputs:
//  - isTag:            the selected channel string starts with "tag:" (pinned).
//  - pinnedTag:        for a tag: channel, the substring after "tag:" (the
//                      requested semver); ignored when isTag is false.
//  - selectedEqInstalled: (getOTAChannel() == getInstalledChannel()) — whether
//                      the currently-selected channel equals the one whose head
//                      is believed to be installed. Only meaningful when !isTag.
//  - installedEmpty:   getInstalledChannel().isEmpty() — a device that missed
//                      the migration backfill. Defensive: treat as "installed ==
//                      selected" so no spurious forced re-flash occurs.
//  - resolvedVersion:  ota->getCurrentVersion() after a synchronous resolve
//                      (checkForUpdates()) of the selected channel's head.
//  - resolveFailed:    the resolve did not confirm a head (network error /
//                      _update_check_failed) — distinct from a non-empty result.
constexpr OtaFlashDecision decideOtaFlash(bool isTag, const char *pinnedTag, bool selectedEqInstalled, bool installedEmpty,
                                          const char *resolvedVersion, bool resolveFailed) {
    if (isTag) {
        // Pinned tag: force only if we confirmed the resolved head equals the
        // pinned tag (leading-`v` tolerant). Otherwise refuse — never flash a
        // tag we cannot confirm.
        if (resolveFailed || otaStrEmpty(resolvedVersion)) {
            return OtaFlashDecision::Refuse;
        }
        return otaVersionsEqualVTolerant(resolvedVersion, pinnedTag) ? OtaFlashDecision::ForceMatchTag : OtaFlashDecision::Refuse;
    }

    // Non-tag channel. A device that missed the migration backfill (empty
    // installedChannel) is treated as installed == selected: no forced re-flash.
    if (installedEmpty || selectedEqInstalled) {
        return OtaFlashDecision::UpgradeOnly;
    }

    // Genuine channel switch. Force-flash the resolved head regardless of semver
    // direction — but only if the new channel's head actually resolved. beta and
    // nightly are moving tags; re-flash-when-current is acceptable/idempotent and
    // correctly advances installedChannel.
    if (resolveFailed || otaStrEmpty(resolvedVersion)) {
        return OtaFlashDecision::Refuse;
    }
    return OtaFlashDecision::ForceChannelSwitch;
}

// Convenience: does this decision mean "pass force=true to ota->update()"?
// Both the pinned-tag confirm and the channel switch force-flash; UpgradeOnly
// runs the normal guard; Refuse does not flash at all.
constexpr bool otaDecisionForces(OtaFlashDecision d) {
    return d == OtaFlashDecision::ForceMatchTag || d == OtaFlashDecision::ForceChannelSwitch;
}

// Convenience wrapper for the specific "should this channel switch force-flash?"
// question the WebUIPlugin asks. Returns true only for a confirmed non-tag switch.
constexpr bool shouldForceFlashForChannelSwitch(bool selectedEqInstalled, bool installedEmpty, const char *resolvedVersion,
                                                bool resolveFailed) {
    return decideOtaFlash(/*isTag=*/false, /*pinnedTag=*/"", selectedEqInstalled, installedEmpty, resolvedVersion,
                          resolveFailed) == OtaFlashDecision::ForceChannelSwitch;
}

// PRO-599: authoritative per-component "is a flash actionable right now?" signal.
//
// The web UI used to RE-DERIVE flash eligibility from (channel, installedChannel,
// status, *UpdateAvailable) and semver ordering. That inference is fragile: it
// silently blocks the Update button whenever `installedChannel` is absent
// (older/unmigrated firmware) or whenever a channel switch resolves to an
// equal/lower semver — even though the firmware's OWN flash decision
// (decideOtaFlash) would happily force-flash it. The result was "Save & Refresh
// accepts the channel but Update Display stays disabled" (PRO-599).
//
// Fix: the device is the authority on what it will actually flash, so it reports
// the decision directly. This helper maps the flash decision to a per-component
// eligibility bool the UI can trust verbatim:
//   - a FORCE decision (confirmed pinned tag OR confirmed channel switch) is
//     eligible regardless of semver direction — this is the exact case the AC
//     calls out ("channel-switch eligibility even when semantic version ordering
//     alone would normally reject it").
//   - UpgradeOnly defers to the semver-gated per-component update-available flag
//     (`componentUpdateAvailable` == ota->isUpdateAvailable(controller)).
//   - Refuse (resolve failed / unconfirmed tag) is never eligible — never
//     surface a flash we cannot confirm.
//
// `componentUpdateAvailable` is the caller's ota->isUpdateAvailable(controller)
// for the specific component (display or controller); it already returns false
// on a failed check, so the Refuse branch and this flag agree on failures.
constexpr bool otaComponentFlashEligible(bool isTag, const char *pinnedTag, bool selectedEqInstalled, bool installedEmpty,
                                         const char *resolvedVersion, bool resolveFailed, bool componentUpdateAvailable) {
    const OtaFlashDecision decision =
        decideOtaFlash(isTag, pinnedTag, selectedEqInstalled, installedEmpty, resolvedVersion, resolveFailed);
    if (decision == OtaFlashDecision::Refuse) {
        return false;
    }
    if (otaDecisionForces(decision)) {
        return true;
    }
    // UpgradeOnly: the normal within-channel path — trust the semver-gated flag.
    return componentUpdateAvailable;
}

// Compile-time truth table — pins the contract so a future edit to the predicate
// fails the firmware compile rather than silently changing the flash decision.
// (constexpr-evaluable cases only; the String/strcmp cases are covered by the
// RUN_TEST suite in test/test_ota_channel_switch_policy/.)
//
// within-channel guard kept (selected == installed, non-tag) -> UpgradeOnly:
static_assert(decideOtaFlash(false, "", /*selEq=*/true, /*instEmpty=*/false, "1.2.3", false) == OtaFlashDecision::UpgradeOnly,
              "PRO-400: within-channel (selected==installed) keeps upgrade-only guard");
// empty installedChannel (missed migration) -> defensive UpgradeOnly:
static_assert(decideOtaFlash(false, "", /*selEq=*/false, /*instEmpty=*/true, "1.2.3", false) == OtaFlashDecision::UpgradeOnly,
              "PRO-400: empty installedChannel treated as equal -> upgrade-only");
// genuine channel switch, resolve OK -> ForceChannelSwitch:
static_assert(decideOtaFlash(false, "", /*selEq=*/false, /*instEmpty=*/false, "1.2.3", false) ==
                  OtaFlashDecision::ForceChannelSwitch,
              "PRO-400: channel switch with resolve OK -> force channel switch");
// channel switch, resolve failed -> Refuse:
static_assert(decideOtaFlash(false, "", /*selEq=*/false, /*instEmpty=*/false, "1.2.3", true) == OtaFlashDecision::Refuse,
              "PRO-400: channel switch with resolve failed -> refuse");
// channel switch, resolve returned empty -> Refuse:
static_assert(decideOtaFlash(false, "", /*selEq=*/false, /*instEmpty=*/false, "", false) == OtaFlashDecision::Refuse,
              "PRO-400: channel switch with empty resolve -> refuse");
// tag path, resolve failed -> Refuse:
static_assert(decideOtaFlash(true, "2.0.8", /*selEq=*/false, /*instEmpty=*/false, "", true) == OtaFlashDecision::Refuse,
              "PRO-400: tag path with resolve failed -> refuse");
// force helper: only the two force decisions force:
static_assert(otaDecisionForces(OtaFlashDecision::ForceMatchTag), "ForceMatchTag forces");
static_assert(otaDecisionForces(OtaFlashDecision::ForceChannelSwitch), "ForceChannelSwitch forces");
static_assert(!otaDecisionForces(OtaFlashDecision::UpgradeOnly), "UpgradeOnly does not force");
static_assert(!otaDecisionForces(OtaFlashDecision::Refuse), "Refuse does not force");

// PRO-599 otaComponentFlashEligible truth table:
// force decisions -> eligible regardless of the semver flag:
static_assert(otaComponentFlashEligible(false, "", /*selEq=*/false, /*instEmpty=*/false, "1.0.0", false, /*upd=*/false),
              "PRO-599: confirmed channel switch is flash-eligible even when semver flag is false");
static_assert(otaComponentFlashEligible(true, "2.0.8", false, false, "2.0.8", false, /*upd=*/false),
              "PRO-599: confirmed pinned tag is flash-eligible even when semver flag is false");
// refuse -> never eligible even if the (stale) semver flag says true:
static_assert(!otaComponentFlashEligible(true, "2.0.8", false, false, "1.9.9", false, /*upd=*/true),
              "PRO-599: unconfirmed tag refuses regardless of stale semver flag");
static_assert(!otaComponentFlashEligible(false, "", false, false, "1.0.0", /*resolveFailed=*/true, /*upd=*/true),
              "PRO-599: failed channel-switch resolve refuses regardless of stale semver flag");
// upgrade-only -> defers to the per-component semver flag:
static_assert(otaComponentFlashEligible(false, "", /*selEq=*/true, false, "1.2.3", false, /*upd=*/true),
              "PRO-599: within-channel eligible iff semver update-available flag is true");
static_assert(!otaComponentFlashEligible(false, "", /*selEq=*/true, false, "1.2.3", false, /*upd=*/false),
              "PRO-599: within-channel not eligible when semver update-available flag is false");
// empty installedChannel (older firmware) defensively maps to upgrade-only, so a
// device that never persisted `ic` still gets the semver-gated within-channel flag:
static_assert(otaComponentFlashEligible(false, "", /*selEq=*/false, /*instEmpty=*/true, "1.2.3", false, /*upd=*/true),
              "PRO-599: empty installedChannel defers to semver flag (upgrade-only)");

#endif // OTACHANNELSWITCHPOLICY_H
