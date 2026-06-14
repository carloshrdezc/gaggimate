# Yield Override Setting — Design Spec

Date: 2026-06-12
Status: Approved (pending implementation plan)
Register: product
Linear: CAR-375

## Problem

The Home dashboard's YIELD field is the most confusing control in the app. Today
its value is seeded once from `localStorage` and only ever syncs *to* the device
when the user drags the slider (`setYield` -> `req:change-brew-target`). It never
re-syncs when a profile is selected. The firmware's `Controller::setBrewTarget()`
rescales the active profile's volumetric target only when that message arrives.

The result is a silent intent trap:

- The displayed YIELD can be stale from a previous profile or session.
- Selecting a profile loads that profile's real target into the firmware, but the
  dashboard keeps showing the old number.
- If the user trusts the displayed number and starts a shot, the machine pulls the
  profile's target, not what the dashboard shows. If the user "fixes" the display
  by nudging the slider to the stale value, they overwrite the correct target.

Either way the user can extract more or less than intended while believing the
opposite. This is purely a web-UI / intent problem; the firmware rescaling logic
itself is correct (CAR-355 / CAR-367).

## Goal

Make yield behavior explicit and safe by default, controlled by a single
device-persisted setting.

- A new boolean setting, `Allow yield override`, lives on the web Settings page.
- Default is **off** (locked). This fixes the reported bug out of the box for the
  display screen and every browser.
- When **off**: the dashboard YIELD is read-only and always reflects the active
  profile's volumetric target. The profile alone stops the shot.
- When **on**: selecting a profile seeds YIELD with that profile's target, and the
  field is editable for per-shot overrides.
- In **both** states, selecting a new profile re-seeds the displayed YIELD to that
  new profile's target, so a custom value never silently carries across a profile
  switch.

## Non-goals

- No change to the firmware volumetric rescaling math (`setVolumetricTarget`,
  `Phase::isFinished`). Those are correct.
- No change to grind target, dose, or any non-yield control.
- No per-profile persistence of override values. The override is per-shot,
  in-memory, exactly as today.
- No new control style. Reuse the existing Settings checkbox rows and the
  dashboard's existing dimmed/disabled affordances.

## Behavior matrix

| Setting | Profile is volumetric | YIELD field | Stops shot at | On profile-select |
|---|---|---|---|---|
| OFF (default) | yes | read-only, dimmed, label `YIELD · LOCKED` | profile target | display updates to new profile target |
| ON | yes | editable `EditableNumBlock`, seeded from profile | override if edited, else profile target | re-seeds to new profile target |
| either | no (time/pressure) | disabled, existing dim treatment | profile durations | n/a (device ignores brew-target) |

No-scale / volumetric-unavailable behavior is unchanged from today; the lock state
still displays.

## Architecture

The feature is a full vertical slice. The existing settings transport is an HTTP
form POST (`request->hasArg(...)` / `request->arg(...)`) with a JSON read-back in
the settings GET handler. A new boolean follows the existing `homekit` /
`momentaryButtons` pattern exactly. The live dashboard reaction uses a new key on
the periodic status broadcast (the same `doc[...]` that already carries `btv`,
`bt`, `bta`).

### 1. Firmware `Settings` (`src/display/core/Settings.h`, `Settings.cpp`)

- Add member `bool allowYieldOverride` defaulting to `false`.
- Add `bool isAllowYieldOverride() const { return allowYieldOverride; }`.
- Add `void setAllowYieldOverride(bool value);` following `setHomekit` (assign +
  `save()`).
- Persist in the existing JSON load/save path alongside the other bool flags,
  defaulting to `false` when the key is absent (so upgraded devices land locked).

### 2. Settings HTTP handlers (`src/display/plugins/WebUIPlugin.cpp`)

- In the settings GET response builder (near `doc["momentaryButtons"] = ...`):
  `doc["allowYieldOverride"] = settings.isAllowYieldOverride();`
- In the settings POST handler (near `settings->setMomentaryButtons(...)`):
  `settings->setAllowYieldOverride(request->hasArg("allowYieldOverride"));`

### 3. Status broadcast (`src/display/plugins/WebUIPlugin.cpp`)

- In the status `doc` builder (near `doc["btv"]` / `doc["bt"]`), add a short key:
  `doc["ayo"] = settings.isAllowYieldOverride() ? 1 : 0;`
- This lets the dashboard react to the setting live without a settings round-trip.

### 4. Firmware enforcement (defense in depth) (`src/display/core/Controller.cpp`)

- In `Controller::setBrewTarget(float value)`, before applying, return early when
  the setting is off:
  `if (!settings.isAllowYieldOverride()) return;`
  placed alongside the existing `if (!profileManager->getSelectedProfile().isVolumetric()) return;`
  guard. This guarantees the display screen and any stale web client honor the
  lock even if a `req:change-brew-target` arrives while the setting is off.

### 5. Web status mapping (`web/src/services/ApiService.js`)

- Map the new key into the status object:
  `allowYieldOverride: !!message.ayo` (default `false` in the initial status
  state object).

### 6. Web Settings page (`web/src/pages/Settings/index.jsx`)

- Add one checkbox row matching the existing `homekit` / `momentaryButtons` rows,
  bound to the form field name `allowYieldOverride`. Label and helper text per the
  Content section.

### 7. Dashboard (`web/src/pages/Home/DashboardMerged.jsx`)

The current YIELD state seeds `yieldTarget` once from `localStorage` and never
re-syncs. Replace that with profile-authoritative seeding:

- Read `allowYieldOverride` (alias `editable`) from status.
- On profile-select (and whenever the broadcast profile target `btv` changes),
  set the displayed yield to the new profile target. This is the core re-seed fix
  and applies in both setting states.
- When `editable` is true: render the normal editable `EditableNumBlock`; commits
  send `req:change-brew-target` as today.
- When `editable` is false: render a read-only variant fed by the profile target
  with the `YIELD · LOCKED` micro-label, no steppers, no click-to-type,
  `cursor: default`, `aria-disabled`, and the locked tooltip.
- Keep the non-volumetric-profile disabled treatment unchanged; it takes
  precedence over the setting (the device ignores brew-target there anyway).

The `localStorage` YIELD value (`gaggimate-target-weight`) is dropped as the
seeding source of truth. The displayed yield is seeded only from the broadcast
profile target (`btv`), in both setting states, to avoid reintroducing the
stale-value class of bug. Removing the localStorage read is part of this change.

## Key states (UI)

1. **Override OFF + volumetric profile (default):** dim read-only number = profile
   target; label `YIELD · LOCKED`.
2. **Override ON + volumetric profile:** editable YIELD seeded from profile; edits
   send `req:change-brew-target`; switching profiles re-seeds.
3. **Non-volumetric profile:** YIELD disabled regardless of the setting; existing
   dim treatment.
4. **No scale / volumetric unavailable:** unchanged; lock state still displays.
5. **Setting toggled while dashboard open:** reacts live via the `ayo` broadcast,
   no reload.

## Interaction model

- Settings checkbox -> existing Save form submit -> persists on device -> next
  status broadcast flips the dashboard.
- Locked YIELD: no hover affordance, no steppers, `aria-disabled`,
  `title="Turn on 'Allow yield override' in Settings to edit per shot"`.
- Motion: only the existing dim transition; no layout animation.

## Content

- Settings label: `Allow yield override`
- Settings helper: `When off, the dashboard yield follows the active profile and can't be edited per shot.`
- Dashboard locked micro-label: `YIELD · LOCKED`
- Locked tooltip: `Turn on 'Allow yield override' in Settings to edit per shot`
- Plain copy, no em dashes. Dimmed number must meet WCAG AA contrast.

## Testing / verification

Host unit tests (`pio test -e native`) for any firmware logic that can run on
host; otherwise manual verification on device. Web build must pass
(`cd web && npm run build`). CI gates per AGENTS.md (cppcheck display + controller,
clang-tidy, native + native-sanitize).

Manual acceptance:

1. Fresh/upgraded device defaults to OFF. YIELD on the dashboard is read-only and
   shows the active profile's target.
2. Select profile A (target 36) then profile B (target 45): displayed YIELD tracks
   36 then 45 in both OFF and ON states.
3. OFF: attempt to edit YIELD -> not possible; start shot -> stops at profile
   target. A stale `req:change-brew-target` (simulated) is ignored by the firmware
   guard.
4. ON: select a profile -> YIELD seeds to its target; edit to a new value -> shot
   stops at the edited value; switch profiles -> YIELD re-seeds (custom value does
   not carry over).
5. Toggle the setting in Settings while the dashboard is open -> dashboard lock
   state flips on the next status broadcast without reload.

## Recommended impeccable references for implementation

- `interaction-design.md` — disabled/locked affordances and a11y
- `color-and-contrast.md` — dimmed-yet-AA locked number
- `clarify.md` — Settings label, helper, and tooltip wording

## Asserted defaults (no open questions)

- Default OFF.
- Firmware double-enforcement in `setBrewTarget` included.
- Re-seed on every profile-select in both states.
- Full on-device version (firmware + web), not web-only.
- Drop the `localStorage` seed as the source of truth; seed from the broadcast
  profile target (`btv`).
