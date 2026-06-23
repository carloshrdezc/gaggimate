#ifndef POSTSTOPGRACEPOLICY_H
#define POSTSTOPGRACEPOLICY_H

// PRO-248: Single source of truth for the post-stop grace window.
//
// Two windows used to govern "what happens after a BLE-scale brew ends":
//
//  1. ShotHistoryPlugin's extended-recording / weight-settle window — the ONLY
//     thing that records the last drops into the shot yield after brew-end.
//  2. BLEScalePlugin's steam scale-alive window — keeps the BLE scale connected
//     and reporting after a scanning-mode -> STEAM transition.
//
// Historically these had two INDEPENDENT durations (3000 ms recording cap vs
// 5000 ms scale-alive) that could (and did) diverge: the recording window closed
// first, so the last drips falling during the remaining scale-alive time landed
// in an already-closed capture window and were lost from the yield (PRO-248
// hardware repro).
//
// The fix unifies both onto this one constant. The extended-recording window is
// the AUTHORITY for "keep capturing drips after brew-end"; the BLE scale-alive
// window is subordinate to it (BLEScalePlugin keeps the scale alive while
// ShotHistory.isExtendedRecording() is true and uses this same value as its hard
// cap). There is no longer a second independent timer that can diverge.
//
// 10 s is the hard cap / maximum grace (PRO-248: bumped from 5 s at Carlos's
// request). A typical shot self-terminates well before this via the
// weight-stabilization early-exit (WEIGHT_STABILIZATION_TIME) in
// ShotHistoryPlugin::record(), so steam still engages promptly in practice.
constexpr unsigned long POST_STOP_GRACE_DURATION_MS = 10000;

#endif // POSTSTOPGRACEPOLICY_H
