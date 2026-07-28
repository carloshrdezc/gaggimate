# PRO-439 Spike — Should the grind SETTING be firmware-authoritative on shots (like `grinderName`)?

**Type:** spike (firmware, web-ui) · **Deliverable:** written recommendation, no behavior change required.
**Base:** `dev-master` @ `a5a61804`. All file:line references below verified live against that tip.

---

## TL;DR / Recommendation

**Split the question in two, because "the grind setting" is really two different values:**

| Value | What it is | Does the device know it today? | Recommendation |
|-------|-----------|--------------------------------|----------------|
| **Machine grind TARGET** (`gt` mode + `gtv` volume / `gtd` duration) | The auto-grind target the GaggiMate itself drives (grams or seconds), already NVS-persisted | **YES** — already authoritative & persisted | **Stamp it onto the shot** (mirror the `grinderName` path). Low cost, high symmetry. |
| **Manual grinder-dial number** (PRO-431) | The physical dial position the user types on the dashboard (`"2.5"`, `"4.1"`, worm-drive clicks…) | **NO** — per-browser localStorage only, never sent to the device | **Keep localStorage-only.** Making it device-authoritative needs a new `req:grind-setting:*` message + new NVS key for a value the device cannot observe. Cost > benefit. |

**Recommended action:** file a follow-up implementation issue to stamp the **machine grind target** onto shot notes under a new field `grindTarget` (structured, not the free-form `grindSetting`), mirroring PRO-428's `grinderName` capture-at-brew-start + fill-only-if-absent pattern. Leave the manual dial number as the per-browser UX log it is today. This closes the "grind info doesn't cross browsers" gap for the value the device actually owns, without inventing a protocol for a value it doesn't.

Scope estimate for the follow-up: **~S (small)** — firmware capture + stamp mirrors an existing pattern almost line-for-line; the only real design decision is the web precedence merge (below).

---

## Background: the three grind-related values and where each lives

### 1. `grinderName` — the PRECEDENT (PRO-428). Device-authoritative. ✅
- Selected grinder **name** stored in `Settings` (NVS key `"sg"`): `Settings.cpp:111`, `getSelectedGrinder()` `Settings.cpp:464`.
- Captured at brew start: `ShotHistoryPlugin::startRecording()` → `currentGrinderName = controller->getSettings().getSelectedGrinder();` — `ShotHistoryPlugin.cpp:704`.
- Stamped fill-only-if-absent at shot end: `handleCompletedShot()` writes `autoNotes["grinderName"]` when null/empty — `ShotHistoryPlugin.cpp:454-458`.
- Web reads device value first: `inferGrinderForShot(shot)` precedence `notes.grinderName → shot.grinderName → notes.grinder → shot.grinder → localStorage guess` — `grinderManager.js:429-435`.
- `"grinderName"` is the contract `isGrinderRecordedForShot` (`grinderManager.js:443-447`, PRO-430) keys off.

**This is the template the spike is asked to mirror.**

### 2. Machine grind TARGET — already device-authoritative & NVS-persisted. ✅ (just not stamped on shots yet)
The device already owns and persists the auto-grind target:
- `targetGrindVolume` → NVS key `"tgv"` (default 18.0g): load `Settings.cpp:58`, save `Settings.cpp:688`, getter `Settings.h:74`.
- `targetGrindDuration` → NVS key `"tgd"` (default 25000ms): load `Settings.cpp:59`, save `Settings.cpp:689`, getter `Settings.h:75`.
- Mode flag: `controller->isVolumetricAvailable() && getSettings().isVolumetricTarget()` decides grams-vs-seconds.
- Emitted in `evt:status` today: `WebUIPlugin.cpp:542-544` → `doc["gtd"]`, `doc["gtv"]`, `doc["gt"]`.
- Web receives them in `ApiService.js:451-453` (`grindTargetDuration`, `grindTargetVolume`, `grindTarget`) and renders them via `formatGrindTarget()` `DashboardMerged.jsx:985-990` (`"18.0g"` / `"25s"`).

Because these are **already in `Settings` and already NVS-persisted**, stamping them onto a shot is essentially free: `ShotHistoryPlugin` can read them at brew start exactly like `getSelectedGrinder()`. **No new protocol, no new NVS key.**

### 3. Manual grinder-dial number — localStorage-only by design (PRO-431). ⚠️
- `MANUAL_GRIND_KEY = 'gaggimate-manual-grind-setting'`, `DEFAULT_MANUAL_GRIND = 0` — `DashboardMerged.jsx:53-54`.
- Explicit design comment: "NEVER sent to the device (unlike `DOSE_KEY`, which also fires `req:dose:set`)" — `DashboardMerged.jsx:50-52`. Dose fires `req:dose:set` (`DashboardMerged.jsx:1393`); the manual grind number does **not** (`DashboardMerged.jsx:1420` just calls `recordManualGrindSetting`, no `api.send`).
- Logged to `MANUAL_GRIND_SETTING_EVENTS_KEY` per browser: `recordManualGrindSetting` `grinderManager.js:361-380`.
- Grinder SELECTION *is* sent (`req:grinders:select`, `DashboardMerged.jsx:1553`) and its captured `grindSetting` is the **machine target label**, not the dial number: `formatGrindTarget(st.grindTarget, …)` `DashboardMerged.jsx:1559`.

The manual dial number is a value the machine **fundamentally cannot observe** — it lives on the physical grinder, not the espresso machine.

---

## Answers to the four spike questions

### Q1. What value would the device record — dial number, machine target, or both?

**Record the machine grind TARGET. Do not record the manual dial number.**

- The **machine target** (`gtv`/`gtd`/`gt`) is already known and persisted on-device (§2). Recording it makes the same info that PRO-431 keeps per-browser cross-client and durable, for zero protocol cost.
- The **manual dial number** is unobservable to the machine. The only way the device could "record" it is if the web client first *pushed* it (a new `req:grind-setting:*`), then the device persisted it to a new NVS key, then stamped it back. That is a lot of new surface (§Q3) to relay a value the device never independently knows — it would just be using the ESP32 as a slower, single-writer replacement for localStorage.

Recording **both** is not worth it: the machine-target stamp is cheap and covers the device-owned value; the dial-number stamp is expensive and covers a value the device can't validate. Ship the cheap half.

### Q2. Where would it be stamped? Field name? Does web precedence change?

**Mirror the `grinderName` path exactly:**

1. **Capture at brew start** — in `ShotHistoryPlugin::startRecording()` (`ShotHistoryPlugin.cpp:666-718`), alongside `currentGrinderName` (L704), snapshot the target into new members, e.g.:
   ```cpp
   currentGrindTargetIsVolumetric = controller->getSettings().isVolumetricTarget();
   currentGrindTargetVolume       = controller->getSettings().getTargetGrindVolume();   // grams
   currentGrindTargetDuration     = controller->getSettings().getTargetGrindDuration(); // ms
   ```
   (All under the existing `stateMutex`, same as the bean/grinder capture.)

2. **Stamp fill-only-if-absent at shot end** — in `handleCompletedShot()` (`ShotHistoryPlugin.cpp:420-474`), next to the `grinderName` block (L454-458), write the resolved label into a **new structured field** rather than the free-form one:
   ```cpp
   // format matches formatGrindTarget(): "18.0g" (volumetric) or "25s" (time)
   if (autoNotes["grindTarget"].isNull() || autoNotes["grindTarget"].as<String>().isEmpty()) {
       autoNotes["grindTarget"] = formatGrindTargetLabel(currentGrindTargetIsVolumetric, …);
       notesChanged = true;
   }
   ```

3. **Field name: `grindTarget`** (NOT `grindSetting`). Rationale — this is the crux of the precedence question:
   - `grindSetting` is the **user's grind-adjustment field** in shot notes: a free-form string the user edits, and the pre-fill target of BOTH the manual dial log AND the machine-target log today (`inferGrindSettingForShot` `grinderManager.js:457-463`). Stamping the machine target directly into `grindSetting` would clobber the semantic that `grindSetting` = "what the user dialed in", and would out-rank the user's own manual entry unless carefully guarded.
   - Using a **separate `grindTarget` notes field** keeps the two concepts distinct: `grindTarget` = "what the machine was set to auto-grind" (device-authoritative), `grindSetting` = "the dial number the user used" (still localStorage / user-entered). They can coexist and even be shown side by side.

4. **Web precedence change** — `inferGrindSettingForShot` (`grinderManager.js:457-463`) currently resolves the free-form dial value: `notes.grindSetting → shot.grindSetting → manual localStorage → machine-target-from-selection-log`. With a device-stamped `grindTarget`:
   - Leave `inferGrindSettingForShot` (the **dial** value) as-is: the manual dial deliberately outranks the machine target (`grinderManager.js:451-456` comment), and that's still correct — a real dial number is more useful than the machine target for the *grind-setting* field.
   - The **fallback** clause (`resolveGrinderSelectionForShot(shot)?.grindSetting`, the localStorage machine-target label) should now prefer the **device-stamped `grindTarget`** over the per-browser selection-log guess, mirroring `inferGrinderForShot`. Concretely, the fallback becomes: `shot.notes.grindTarget (device) → localStorage selection-log label`.
   - Add an `isGrindTargetRecordedForShot(shot)` guard (mirror `isGrinderRecordedForShot`, `grinderManager.js:443-447`) keying off `notes.grindTarget`, so the UI can badge the machine target as device-recorded vs guessed — symmetry with PRO-430.

   **Net:** the user-facing `grindSetting` (dial) precedence is unchanged; only the *machine-target fallback* becomes device-authoritative instead of per-browser. No behavior regression for users who type a dial number.

### Q3. Protocol change? Is device-authoritative manual dial worth it?

**Only the manual dial would require protocol changes — and it is NOT worth it.**

Machine target (recommended): **zero protocol change**. `gtv`/`gtd`/`gt` already flow in `evt:status`; the values already live in `Settings`/NVS. Firmware just reads existing getters at brew start.

Manual dial (not recommended): would require, at minimum —
- A **new WebSocket request** `req:grind-setting:set` (+ a `res:` ack), parallel to `req:dose:set`, wired in `WebUIPlugin.cpp` and `ApiService.js`, with a corresponding `evt:status` field so other clients see it.
- A **new NVS key** in `Settings` (e.g. `"mgs"`) + getter/setter + the cross-task `selectedNameMutex`-style guard already used for bean/grinder strings (`Settings.h:279-281`), because the WebSocket-handler task writes it while the display/loop task reads it during shot capture (same hazard PRO-427 fixed).
- New `ShotHistoryPlugin` capture + stamp for it.
- Web: change `recordManualGrindSetting` to also `api.send(...)`, and re-root `inferGrindSettingForShot` on the device value.

**Cost/benefit:** that is a full feature (new message type + new persisted setting + concurrency guard + web plumbing) whose only benefit is relaying a value the device cannot independently observe or validate — it would just centralize localStorage state in NVS. The manual dial is inherently a *per-user, per-session physical adjustment*; a single device-global NVS slot doesn't even model it well (two users, two dial numbers). **Keep it localStorage-only.** If cross-browser persistence of the dial number is ever wanted, the cheaper path is to persist it against the shot from the web client on save (it already can write `notes.grindSetting`), not to make the firmware authoritative.

### Q4. Recommendation + scope + follow-up issue

**Recommendation:** implement the machine-grind-target stamp (device-authoritative, mirrors PRO-428). Do not implement device-authoritative manual dial.

**Scope:** ~S. One firmware capture-and-stamp (pattern copied from `grinderName`), one new notes field `grindTarget`, one small web precedence/guard addition. No new protocol, no new NVS.

**Proposed follow-up implementation issue** (spec below) — orchestrator/Carlos to file.

---

## Proposed follow-up issue spec (ready to file)

> **Title:** feat(firmware+web): stamp machine grind TARGET onto shot notes (`grindTarget`), device-authoritative — follow-up to PRO-439
>
> **Labels:** feature, firmware, web-ui
>
> **What:** Mirror PRO-428's `grinderName` capture so the machine grind target (auto-grind grams/seconds) is recorded on each shot and read authoritatively across clients.
>
> **Firmware (`src/display/plugins/ShotHistoryPlugin.{h,cpp}`):**
> - Add members `currentGrindTargetIsVolumetric` (bool), `currentGrindTargetVolume` (double, g), `currentGrindTargetDuration` (int, ms).
> - In `startRecording()` (`ShotHistoryPlugin.cpp:704` area), under `stateMutex`, snapshot from `getSettings().isVolumetricTarget()`, `getTargetGrindVolume()`, `getTargetGrindDuration()`.
> - In `handleCompletedShot()` (`ShotHistoryPlugin.cpp:454-458` area), fill-only-if-absent `autoNotes["grindTarget"]` with the formatted label (`"18.0g"` / `"25s"`), matching `formatGrindTarget()` output (`DashboardMerged.jsx:985-990`). Extend the `if (!currentBeanName.isEmpty() || !currentGrinderName.isEmpty())` guard (`ShotHistoryPlugin.cpp:432`) to also fire when a grind target is present.
> - **No new NVS key, no new WebSocket message** — `tgv`/`tgd` already persisted (`Settings.cpp:58-59,688-689`).
>
> **Web (`web/src/utils/grinderManager.js`):**
> - New `isGrindTargetRecordedForShot(shot)` keying off `shot.notes.grindTarget` (mirror `isGrinderRecordedForShot` `grinderManager.js:443-447`).
> - In `inferGrindSettingForShot` (`grinderManager.js:457-463`), change ONLY the final fallback: prefer `shot.notes.grindTarget` (device) over `resolveGrinderSelectionForShot(shot)?.grindSetting` (per-browser log). Manual-dial precedence (`grinderManager.js:451-456`) unchanged.
> - Optionally surface `grindTarget` as a distinct read-only "machine target" line in Shot Notes UI, separate from the editable `grindSetting`.
>
> **Explicitly out of scope (per PRO-439):** making the manual grinder-dial number device-authoritative (would need `req:grind-setting:*` + new NVS key + concurrency guard for a value the device can't observe — not worth it).
>
> **Acceptance:** a shot pulled with an auto-grind target set carries `notes.grindTarget` written by firmware; a second browser that never saw the selection reads the same machine target; user-entered dial numbers still take precedence in the `grindSetting` field. CI gates green.

---

## Assumptions made (no design fork left to Carlos, per spike brief)

1. **New field name `grindTarget`** (not overloading `grindSetting`) — chosen to keep "machine target" (device-owned) distinct from "dial number" (user-owned) and avoid clobbering the user's editable field. If a single merged field is preferred later, `inferGrindSettingForShot`'s fallback can be pointed at it, but the two-field split is the safer default.
2. **Label format stamped as a string** (`"18.0g"`/`"25s"`) rather than structured numeric fields — matches how the value is already displayed (`formatGrindTarget`) and how `grindSetting` notes are stored (free-form string, `docs/shot-notes-api.md`). Numeric fields would be more queryable but add schema surface for no current consumer.
3. **Manual dial stays localStorage-only** — treated as the settled recommendation, not an open question, per the cost/benefit in Q3.

## Verdict: VALIDATED (recommendation is doc-only; no code change shipped in this spike)

- **What worked:** confirmed the machine grind target is already device-authoritative + NVS-persisted, so mirroring the `grinderName` path is near-free; confirmed the manual dial is deliberately localStorage-only and would need a full new protocol to change.
- **What didn't / caveat:** the naive reading of the issue ("stamp *the* grind setting") is ambiguous — there are two values. Stamping the machine target is right; stamping the dial number is not worth it. The doc resolves the ambiguity.
- **Recommendation for the real build:** file the follow-up above; keep it ~S; do not touch the manual-dial protocol.

---

> **Update (PRO-603):** This spike recommended keeping the manual dial localStorage-only. That call was later superseded — PRO-603 shipped the previously-declined path, making the manual grind setting firmware-authoritative (NVS-persisted, broadcast as `mg` in `evt:status`, set via `req:manual-grind:set`) for cross-browser UX consistency with DOSE. The reasoning here (device "can't observe" the physical dial) still holds; PRO-603 accepts that the stored value is the last user-entered number, treated exactly like DOSE.
