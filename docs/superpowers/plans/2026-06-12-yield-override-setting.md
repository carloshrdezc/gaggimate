# Yield Override Setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a device-persisted `Allow yield override` setting (default OFF) that locks the dashboard YIELD field to the active profile's target, and fix the dashboard so the displayed yield always tracks the selected profile instead of a stale localStorage value.

**Architecture:** Firmware gains one persisted bool (`allowYieldOverride`) following the existing `homekit`/`momentaryButtons` pattern, exposed over the settings HTTP GET/POST and the periodic status broadcast (`ayo`). `Controller::setBrewTarget()` ignores override messages when the setting is off (defense in depth). The web Settings page gets one `nd-toggle` row; `ApiService` maps the new status key; the dashboard reseeds YIELD from the broadcast profile target on every profile change and renders the field read-only when override is off.

**Tech Stack:** C++ (ESP32 / Arduino `Preferences`, ArduinoJson), Preact + `@preact/signals`, Vite.

**Linear:** CAR-375. PRs target `dev-master`. Spec: `docs/superpowers/specs/2026-06-12-yield-override-setting-design.md`.

**Build/verify commands (from repo root):**
- Firmware build: `~/.platformio/penv/Scripts/pio.exe run -e display`
- Host tests: `~/.platformio/penv/Scripts/pio.exe test -e native`
- Web build: `cd web && npm run build`
- Do NOT run `scripts/format.sh` (it reformats `src/display/ui/**` fonts). Format touched C++ files individually with clang-format if needed.

---

## File Structure

**Firmware:**
- `src/display/core/Settings.h` — add member `allowYieldOverride`, getter `isAllowYieldOverride()`, setter decl `setAllowYieldOverride(bool)`.
- `src/display/core/Settings.cpp` — load from Preferences (`ayo`), save to Preferences, define setter.
- `src/display/plugins/WebUIPlugin.cpp` — settings GET read-back, settings POST write, status broadcast `ayo`.
- `src/display/core/Controller.cpp` — `setBrewTarget()` guard.

**Web:**
- `web/src/services/ApiService.js` — map `message.ayo` into status; add default in initial state.
- `web/src/pages/Settings/index.jsx` — toggle row + form submit set/delete + onChange branch.
- `web/src/pages/Home/DashboardMerged.jsx` — reseed-from-profile logic + locked/editable YIELD render.

---

## Task 1: Firmware Settings field, getter, setter, persistence

**Files:**
- Modify: `src/display/core/Settings.h:74-75` (getters), `:148` (setter decl), `:217` (member)
- Modify: `src/display/core/Settings.cpp:59` (load), `:586` (save), `:267-270` (setter def)

- [ ] **Step 1: Add the member field**

In `src/display/core/Settings.h`, after line 217 (`bool volumetricTarget = false;`):

```cpp
    bool volumetricTarget = false;
    bool allowYieldOverride = false;
```

- [ ] **Step 2: Add the getter**

In `src/display/core/Settings.h`, after line 75 (`bool isVolumetricTarget() const { return volumetricTarget; }`):

```cpp
    bool isVolumetricTarget() const { return volumetricTarget; }
    bool isAllowYieldOverride() const { return allowYieldOverride; }
```

- [ ] **Step 3: Add the setter declaration**

In `src/display/core/Settings.h`, after line 148 (`void setVolumetricTarget(bool volumetric_target);`):

```cpp
    void setVolumetricTarget(bool volumetric_target);
    void setAllowYieldOverride(bool allow_yield_override);
```

- [ ] **Step 4: Load from Preferences (default false)**

In `src/display/core/Settings.cpp`, after line 59 (`volumetricTarget = preferences.getBool("vt", false);`):

```cpp
    volumetricTarget = preferences.getBool("vt", false);
    allowYieldOverride = preferences.getBool("ayo", false);
```

- [ ] **Step 5: Save to Preferences**

In `src/display/core/Settings.cpp`, after line 586 (`preferences.putBool("vt", volumetricTarget);`):

```cpp
    preferences.putBool("vt", volumetricTarget);
    preferences.putBool("ayo", allowYieldOverride);
```

- [ ] **Step 6: Define the setter**

In `src/display/core/Settings.cpp`, after the `setVolumetricTarget` definition (ends line 270):

```cpp
void Settings::setAllowYieldOverride(bool allow_yield_override) {
    this->allowYieldOverride = allow_yield_override;
    save();
}
```

- [ ] **Step 7: Build firmware to verify it compiles**

Run: `~/.platformio/penv/Scripts/pio.exe run -e display`
Expected: build succeeds (no errors referencing `allowYieldOverride`).

- [ ] **Step 8: Commit**

```bash
git add src/display/core/Settings.h src/display/core/Settings.cpp
git commit -m "feat(settings): add persisted allowYieldOverride flag (CAR-375)"
```

---

## Task 2: Settings HTTP GET/POST plumbing + status broadcast

**Files:**
- Modify: `src/display/plugins/WebUIPlugin.cpp:241` (status broadcast), `:989` (POST write), `:1120` (GET read-back)

- [ ] **Step 1: Add the status broadcast key**

In `src/display/plugins/WebUIPlugin.cpp`, after line 241 (`doc["btv"] = profileManager->getSelectedProfile().getTotalVolume(); ...`):

```cpp
        doc["btv"] = profileManager->getSelectedProfile().getTotalVolume(); // raw volumetric target for frontend Weight card
        doc["ayo"] = controller->getSettings().isAllowYieldOverride() ? 1 : 0;
```

- [ ] **Step 2: Add the settings POST write**

In `src/display/plugins/WebUIPlugin.cpp`, after line 989 (`settings->setMomentaryButtons(request->hasArg("momentaryButtons"));`):

```cpp
            settings->setMomentaryButtons(request->hasArg("momentaryButtons"));
            settings->setAllowYieldOverride(request->hasArg("allowYieldOverride"));
```

- [ ] **Step 3: Add the settings GET read-back**

In `src/display/plugins/WebUIPlugin.cpp`, after line 1120 (`doc["momentaryButtons"] = settings.isMomentaryButtons();`):

```cpp
    doc["momentaryButtons"] = settings.isMomentaryButtons();
    doc["allowYieldOverride"] = settings.isAllowYieldOverride();
```

- [ ] **Step 4: Build firmware to verify it compiles**

Run: `~/.platformio/penv/Scripts/pio.exe run -e display`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/display/plugins/WebUIPlugin.cpp
git commit -m "feat(webui): expose allowYieldOverride over settings + status (CAR-375)"
```

---

## Task 3: Firmware enforcement guard in setBrewTarget

**Files:**
- Modify: `src/display/core/Controller.cpp:755-768` (`setBrewTarget`)

**Context:** Current `setBrewTarget` (lines 755-768) returns early when the profile isn't volumetric, then calls `setVolumetricTarget`. Add a second early-return when the override setting is off, so a stale or rogue `req:change-brew-target` is ignored device-wide (display + any web client).

- [ ] **Step 1: Add the setting guard**

In `src/display/core/Controller.cpp`, modify `setBrewTarget` so the guards read:

```cpp
void Controller::setBrewTarget(float value) {
    // Apply absolute brew target from the dashboard YIELD slider. When the
    // active profile is volumetric, set the cumulative target across brew
    // phases. Otherwise the dashboard YIELD has no meaning — ignore so we
    // don't accidentally rewrite a time-based profile's duration. Mirrors
    // the in-memory mutation pattern of raise/lowerBrewTarget — the change
    // applies to the next shot but is NOT persisted to disk; reloading the
    // profile restores the saved target.
    //
    // CAR-375: when the per-shot yield override is disabled, the yield is
    // locked to the profile. Ignore any incoming brew-target so the display
    // and any stale web client honor the lock.
    if (!settings.isAllowYieldOverride()) {
        return;
    }
    if (!profileManager->getSelectedProfile().isVolumetric()) {
        return;
    }
    profileManager->getSelectedProfile().setVolumetricTarget(value);
    handleProfileUpdate();
}
```

(Confirm `settings` is the in-scope member used elsewhere in Controller.cpp, e.g. `settings.isVolumetricTarget()` at line 771. It is.)

- [ ] **Step 2: Build firmware to verify it compiles**

Run: `~/.platformio/penv/Scripts/pio.exe run -e display`
Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/display/core/Controller.cpp
git commit -m "feat(controller): ignore brew-target when yield override is off (CAR-375)"
```

---

## Task 4: Web status mapping in ApiService

**Files:**
- Modify: `web/src/services/ApiService.js:321` (status map), `:373` (initial state object)

**Context:** `_handleStatus` builds `newStatus` from the incoming message (lines 308-336). The initial/default status object lives near line 369-376 (where `brewTargetVolume: 0` etc. are defined).

- [ ] **Step 1: Map the broadcast key into status**

In `web/src/services/ApiService.js`, after line 321 (`brewTargetVolume: message.btv || 0,`):

```javascript
      brewTargetVolume: message.btv || 0,
      allowYieldOverride: !!message.ayo,
```

- [ ] **Step 2: Add the default in the initial status object**

In `web/src/services/ApiService.js`, in the default status object (after `brewTargetVolume: 0,` near line 373):

```javascript
    brewTargetVolume: 0,
    allowYieldOverride: false,
```

- [ ] **Step 3: Verify web build**

Run: `cd web && npm run build`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add web/src/services/ApiService.js
git commit -m "feat(web): map allowYieldOverride status key (CAR-375)"
```

---

## Task 5: Settings page toggle row

**Files:**
- Modify: `web/src/pages/Settings/index.jsx:109-110` (onChange branch), `:193-194` (submit set/delete), `:524-537` (toggle row markup)

**Context:** `formData` is spread from `fetchedSettings` (line 41-48), so `allowYieldOverride` flows in automatically once the firmware GET returns it. The toggle uses the existing `nd-toggle` button pattern. The `onChange(key)` handler is a curried setter; momentary buttons uses an explicit branch (lines 109-110) — add a matching branch. The form submit explicitly sets/deletes boolean keys (lines 189-196) because `hasArg()` treats any present key as true — add a set/delete pair.

- [ ] **Step 1: Add the onChange branch**

In `web/src/pages/Settings/index.jsx`, after line 110 (the `momentaryButtons` branch closing):

```javascript
      } else if (key === 'momentaryButtons') {
        setFormData(prev => ({ ...prev, momentaryButtons: !prev.momentaryButtons }));
      } else if (key === 'allowYieldOverride') {
        setFormData(prev => ({ ...prev, allowYieldOverride: !prev.allowYieldOverride }));
```

- [ ] **Step 2: Add the submit set/delete pair**

In `web/src/pages/Settings/index.jsx`, after line 194 (the `momentaryButtons` else/delete):

```javascript
        if (formData.momentaryButtons) formDataToSubmit.set('momentaryButtons', '1');
        else formDataToSubmit.delete('momentaryButtons');
        if (formData.allowYieldOverride) formDataToSubmit.set('allowYieldOverride', '1');
        else formDataToSubmit.delete('allowYieldOverride');
```

- [ ] **Step 3: Add the toggle row markup**

In `web/src/pages/Settings/index.jsx`, immediately after the momentary-switches row's closing `</div>` (line 537), inside the same settings card container:

```jsx
              <div className='flex items-center justify-between'>
                <span className='font-nd-mono text-[14px] text-[var(--text-primary,#e8e8e8)]'>
                  Allow yield override
                </span>
                <button
                  type='button'
                  className={`nd-toggle ${formData.allowYieldOverride ? 'nd-toggle--active' : ''}`}
                  onClick={onChange('allowYieldOverride')}
                  role='switch'
                  aria-checked={!!formData.allowYieldOverride}
                >
                  <span className='nd-toggle-thumb' />
                </button>
              </div>
              <p className='font-nd-mono text-[12px] text-[var(--text-secondary,#999)] -mt-2'>
                When off, the dashboard yield follows the active profile and can't be edited per shot.
              </p>
```

- [ ] **Step 4: Verify web build**

Run: `cd web && npm run build`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add web/src/pages/Settings/index.jsx
git commit -m "feat(settings-ui): add Allow yield override toggle (CAR-375)"
```

---

## Task 6: Dashboard reseed-from-profile + locked/editable YIELD

**Files:**
- Modify: `web/src/pages/Home/DashboardMerged.jsx:1186-1195` (yield state + setter), `:657-768` (EditableNumBlock — add disabled support), `:1874-1883` (YIELD render site)

**Context:**
- `s = status.value` (line 1035); `s.brewTargetVolume` is the live profile target; `s.selectedProfileId` changes on profile select; `s.allowYieldOverride` is the new flag.
- Today `yieldTarget` seeds once from localStorage (lines 1187-1189) and `setYield` writes localStorage + sends `req:change-brew-target` (lines 1190-1195).
- `EditableNumBlock` (lines 657-768) has no disabled mode; add one.
- YIELD is rendered at lines 1874-1883 via `<EditableNumBlock label='YIELD' .../>`.

- [ ] **Step 1: Replace the yield state with profile-authoritative seeding**

In `web/src/pages/Home/DashboardMerged.jsx`, replace lines 1186-1195 (the `// Target yield` block through the end of `setYield`) with:

```javascript
  // Target yield — profile-authoritative. The displayed yield always tracks the
  // active profile's volumetric target (s.brewTargetVolume), reseeding whenever
  // the profile changes. localStorage is intentionally NOT used as the seed
  // source (CAR-375) to avoid stale-value bugs. When override is allowed the
  // field is editable and commits send req:change-brew-target; when not, it is
  // read-only.
  const yieldEditable = !!s.allowYieldOverride;
  const [yieldTarget, setYieldTargetState] = useState(() => s.brewTargetVolume || DEFAULT_YIELD);

  // Reseed to the active profile's target on profile change (and whenever the
  // device's broadcast target changes). Applies in both editable and locked
  // states so a custom value never silently carries across a profile switch.
  useEffect(() => {
    if (s.brewTargetVolume > 0) {
      setYieldTargetState(s.brewTargetVolume);
    }
  }, [s.selectedProfileId, s.brewTargetVolume]);

  const setYield = useCallback(val => {
    if (!yieldEditable) return;
    const v = Math.max(5, Math.min(120, val));
    setYieldTargetState(v);
    try { api.send({ tp: 'req:change-brew-target', target: v }); } catch {}
  }, [api, yieldEditable]);
```

(Note: `useEffect` is already imported at line 4. The `YIELD_KEY` constant at line 42 becomes unused after this change — leave it; a later optional cleanup task removes it to keep this diff focused.)

- [ ] **Step 2: Add disabled support to EditableNumBlock**

In `web/src/pages/Home/DashboardMerged.jsx`, change the `EditableNumBlock` signature (line 658) to accept `disabled` and `lockedHint`:

```javascript
function EditableNumBlock({ label, value, unit, hint, accent, step, min, max, onCommit, disabled = false, lockedHint }) {
```

Then in its returned JSX, gate interactivity on `disabled`. Replace the non-editing display block (lines 729-768, the `) : (` branch) so that when `disabled` is true the number is dimmed, non-clickable, the steppers are omitted, and the label shows the locked hint. Concretely, change the label div (lines 686-696) to append the locked hint:

```jsx
      <div
        style={{
          fontFamily: 'var(--dm-font-mono)',
          fontSize: 9,
          letterSpacing: '0.18em',
          color: 'var(--dm-fg-dim)',
          marginBottom: 3,
        }}
      >
        {disabled && lockedHint ? lockedHint : label}
      </div>
```

And wrap the editing/non-editing branch so a disabled block renders a static, dimmed number:

```jsx
      {disabled ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span
            aria-disabled='true'
            title="Turn on 'Allow yield override' in Settings to edit per shot"
            style={{
              fontFamily: 'var(--dm-font-display)',
              fontSize: 28,
              color: 'var(--dm-fg-faint)',
              fontWeight: 700,
              fontVariantNumeric: 'tabular-nums',
              lineHeight: 1,
              cursor: 'default',
            }}
          >
            {typeof value === 'number' ? value.toFixed(1) : value}
          </span>
          <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-faint)' }}>
            {unit}
          </span>
        </div>
      ) : editing ? (
```

(The existing `editing ? (...) : (...)` becomes the `editing` and final `else` arms of this three-way conditional. Keep the existing editing-input block and the existing non-editing interactive block unchanged as the other two arms.)

- [ ] **Step 3: Wire the YIELD render site**

In `web/src/pages/Home/DashboardMerged.jsx`, update the YIELD `EditableNumBlock` (lines 1874-1883) to pass the new props:

```jsx
            <EditableNumBlock
              label='YIELD'
              value={targetWeight}
              unit='g'
              hint={`1 : ${(targetWeight / Math.max(dose, 1)).toFixed(2)}`}
              step={0.5}
              min={5}
              max={120}
              onCommit={setYield}
              disabled={!yieldEditable}
              lockedHint='YIELD · LOCKED'
            />
```

- [ ] **Step 4: Verify web build**

Run: `cd web && npm run build`
Expected: build succeeds.

- [ ] **Step 5: Manual smoke check (describe, not automated)**

With a connected device or simulated status: confirm that (a) with `allowYieldOverride` false the YIELD number is dimmed, shows `YIELD · LOCKED`, has no steppers, and isn't clickable; (b) selecting a different profile changes the displayed number; (c) with the flag true the field is editable and commits send `req:change-brew-target`.

- [ ] **Step 6: Commit**

```bash
git add web/src/pages/Home/DashboardMerged.jsx
git commit -m "feat(dashboard): profile-authoritative yield + lock when override off (CAR-375)"
```

---

## Task 7: QA self-review and full CI gate run

**Files:** none (verification only)

- [ ] **Step 1: Re-read the full diff**

Run: `git diff dev-master...HEAD --stat` then `git diff dev-master...HEAD`
Verify every change maps to a spec requirement; no stray edits.

- [ ] **Step 2: Firmware build**

Run: `~/.platformio/penv/Scripts/pio.exe run -e display`
Expected: success.

- [ ] **Step 3: Host tests**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native`
Expected: pass (no new failures; this change has no host-testable logic but the suite must stay green).

- [ ] **Step 4: Web build**

Run: `cd web && npm run build`
Expected: success.

- [ ] **Step 5: cppcheck gates (match CI)**

Run:
```bash
~/.platformio/penv/Scripts/pio.exe check -e display    --fail-on-defect=medium -f "-<*>" -f "+<src/display/>" -f "-<src/display/ui>"
~/.platformio/penv/Scripts/pio.exe check -e controller --fail-on-defect=medium -f "-<*>" -f "+<src/controller/>"
```
Expected: no medium+ defects introduced by the changed files.

- [ ] **Step 6: Move CAR-375 to QA, then Ready for Testing on PR open**

Per AGENTS.md flow: this is the QA gate. If all the above pass, the work is ready for a PR against `dev-master` with `Fixes CAR-375` in the description.

---

## Self-Review (completed during planning)

**1. Spec coverage:**
- Firmware Settings field/getter/setter/persist → Task 1 ✓
- Settings GET/POST plumbing → Task 2 ✓
- Status broadcast `ayo` → Task 2 ✓
- `setBrewTarget` enforcement guard → Task 3 ✓
- ApiService status mapping → Task 4 ✓
- Settings page toggle + copy → Task 5 ✓
- Dashboard reseed-on-profile-select (both states) → Task 6 Step 1 ✓
- Locked vs editable YIELD render → Task 6 Steps 2-3 ✓
- Drop localStorage seed → Task 6 Step 1 ✓
- Non-volumetric profile precedence → covered: the existing non-volumetric disabled treatment is unchanged and independent; when override is off the block is also disabled. Both paths render disabled, no conflict. (No separate task needed; verify in Task 6 Step 5.)
- Live reaction on toggle → status broadcast (Task 2) + reactive `s.allowYieldOverride` (Task 6) ✓

**2. Placeholder scan:** No TBD/TODO; every code step shows real code. The `YIELD_KEY`-now-unused note is explicit, not a placeholder.

**3. Type/name consistency:** firmware `allowYieldOverride` / `isAllowYieldOverride` / `setAllowYieldOverride` / pref key `ayo`; broadcast key `ayo`; web status `allowYieldOverride`; Settings form key `allowYieldOverride`; dashboard `yieldEditable`. Consistent across all tasks.
