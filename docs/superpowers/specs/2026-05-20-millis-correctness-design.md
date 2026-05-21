# Firmware `millis()` Correctness Pair — Design

**Date:** 2026-05-20
**Linear issues:** CAR-102, CAR-103
**Project:** Gaggimate
**Target branch:** `dev-master`

## Context

Two firmware bugs (both P2) where `millis()` is used incorrectly. They share a
theme but the failure modes and fixes are very different:

| Issue   | Symptom                                                                    | Fix size |
|---------|-----------------------------------------------------------------------------|----------|
| CAR-103 | `WebUIPlugin::loop()` time math breaks at ~24.85 days (signed overflow) and again at ~49.7 days (`millis()` wrap). Four timers either spam or freeze. | ~10 LOC, single file pair |
| CAR-102 | `BeanManager` persists `millis()` to JSON. Bean sort order corrupts every reboot; "last updated" timestamps are meaningless. | ~30 LOC firmware + ~5 LOC web + heuristic migration |

Both were filed under the "memory/lifetime" cluster's adjacent priorities and
chosen because they're firmware reliability bugs that pair naturally for a
single design pass.

## Non-Goals

- No broader audit of `millis()` usage elsewhere — exploration confirmed every
  other plugin already uses the rollover-safe pattern (`now - lastX > INTERVAL`
  with `unsigned long` storage). The bug is contained to these two sites.
- No introduction of an absolute-time abstraction layer or a `WallClock` class.
  The codebase already has the patterns we need (`time(&now)` for persisted
  timestamps; `unsigned long now = millis()` for elapsed-time math).
- No `.gitignore` changes, no test framework, no doc rewrites.

---

## CAR-103 — `WebUIPlugin` `millis()` rollover bugs

### Root cause

Two compounding defects in `src/display/plugins/WebUIPlugin.{h,cpp}`:

1. **Signed `long` storage.** `WebUIPlugin.h:92-95` declares four timing
   fields as `long`, and `WebUIPlugin.cpp:155` reads `const long now = millis();`.
   `millis()` returns `unsigned long`. At ~24.85 days uptime, the assignment
   reinterprets the high bit as a sign bit; `now` becomes negative.

2. **Forward-addition comparison.** Lines 156, 162, 259, 263 use
   `now > lastX + INTERVAL` instead of the rollover-safe
   `now - lastX > INTERVAL`. With `INTERVAL` declared `constexpr size_t`,
   the implicit conversion chain (signed `long` → unsigned `size_t` → comparison)
   misbehaves near both signed and unsigned overflow boundaries.

### Failure modes

The four guarded code blocks behave correctly for the first ~24 days, then:

| Field | Block | Failure at ~24.85 days | Failure at ~49.7 days |
|-------|-------|------------------------|------------------------|
| `lastUpdateCheck` | OTA update check (5 min) | Spams `ota->checkForUpdates()` every loop tick | Stops checking until reboot |
| `lastStatus` | WebSocket status broadcast (500 ms) | Floods clients (`setCloseClientOnQueueFull(true)` evicts them) | UI stops updating temp/pressure |
| `lastCleanup` | WebSocket cleanup (5 s) | Runs every tick (CPU waste) | Zombie WS clients accumulate |
| `lastDns` | Captive-portal DNS (10 ms) | Runs every tick | AP-mode users can't reach the device |

### Fix

Three coordinated edits in two files:

#### 1. Field types — `src/display/plugins/WebUIPlugin.h:92-95`

Change:
```cpp
long lastUpdateCheck = 0;
long lastStatus = 0;
long lastCleanup = 0;
long lastDns = 0;
```

To:
```cpp
unsigned long lastUpdateCheck = 0;
unsigned long lastStatus = 0;
unsigned long lastCleanup = 0;
unsigned long lastDns = 0;
```

#### 2. Local `now` declaration — `src/display/plugins/WebUIPlugin.cpp:155`

Change:
```cpp
const long now = millis();
```

To:
```cpp
const unsigned long now = millis();
```

#### 3. Comparisons — `src/display/plugins/WebUIPlugin.cpp:156, 162, 259, 263`

Change line 156 from:
```cpp
if ((lastUpdateCheck == 0 || now > lastUpdateCheck + UPDATE_CHECK_INTERVAL)) {
```
To:
```cpp
if (lastUpdateCheck == 0 || now - lastUpdateCheck > UPDATE_CHECK_INTERVAL) {
```

Change line 162 from:
```cpp
if (now > lastStatus + STATUS_PERIOD && (!ws.getClients().empty() || relayConnected)) {
```
To:
```cpp
if (now - lastStatus > STATUS_PERIOD && (!ws.getClients().empty() || relayConnected)) {
```

Change line 259 from:
```cpp
if (now > lastCleanup + CLEANUP_PERIOD) {
```
To:
```cpp
if (now - lastCleanup > CLEANUP_PERIOD) {
```

Change line 263 from:
```cpp
if (now > lastDns + DNS_PERIOD && dnsServer != nullptr) {
```
To:
```cpp
if (now - lastDns > DNS_PERIOD && dnsServer != nullptr) {
```

This matches the established codebase pattern in `BLEScalePlugin.cpp:110-114`,
`AutoWakeupPlugin.cpp:34`, `ShotHistoryPlugin.cpp:512`,
`BeanconquerorPlugin.cpp:75`, and `Controller.cpp:311`.

### Risk

None — strictly more correct. Subtraction of unsigned integers is well-defined
modulo 2^32, so `now - lastX` always yields the elapsed milliseconds even when
`millis()` has wrapped between the two reads.

### Verification

- `pio run -e display` clean.
- Manual on hardware: not feasible to reproduce the bug (would require 25+ days
  uptime). Verification reduces to code review against the established
  pattern.

---

## CAR-102 — `BeanManager` timestamps across reboots

### Root cause

`BeanManager::saveBean()` (`src/display/core/BeanManager.cpp:78`) writes
`millis()` (uptime since boot) into JSON files persisted on SPIFFS:

```cpp
const unsigned long now = millis();
if (bean.createdAt == 0) {
    bean.createdAt = now;
}
bean.updatedAt = now;
```

`writeBean()` (`BeanManager.h:65-66`) serializes both fields as plain integers.
`parseBean()` (`BeanManager.h:46-47`) reads them back. `BeanManager::listBeans()`
(`BeanManager.cpp:44`) and `web/src/utils/beanManager.js:67` sort by
`updatedAt DESC`. The web normalizer (`beanManager.js:46-62`) treats the values
as `Date.now()`-style epoch ms.

Three concrete failure modes:

1. **Sort order corrupts every reboot.** A bean edited 5 s post-boot has
   `updatedAt ≈ 5000`; a bean edited 3 h pre-reboot has
   `updatedAt ≈ 10800000`. The freshly-edited bean sorts to the *bottom*.

2. **"Last updated" renders as 1970.** Any future UI that does
   `new Date(bean.updatedAt)` on a `millis()` value yields 1970-01-01.

3. **Inconsistent units within a single record.** Web's `saveBean` writes
   `Date.now()` (Unix ms) into `updatedAt` while the device's `createdAt`
   coming back is still `millis()`-since-some-past-boot. Same JSON object,
   different units.

### Background — established patterns

The firmware already has wall-clock infrastructure:

- NTP is initialized in `Controller.cpp:246-251` via `configTzTime` + `sntp_init`.
- `AutoWakeupPlugin::isTimeValid()` (`AutoWakeupPlugin.cpp:74-82`) is the
  canonical "has NTP synced" check (year > 2020).
- `ShotHistoryPlugin::getTime()` (`ShotHistoryPlugin.cpp:571-575`) is the
  canonical "save Unix seconds to disk" helper.
- `MQTTPlugin.cpp:115` and `AutoWakeupPlugin.cpp:76` both use
  `time(&now)` returning `time_t` Unix seconds.

`BeanManager.cpp` is the *only* place in the firmware where a `millis()`
value is persisted to JSON. The fix aligns BeanManager with the rest of the
codebase.

### Decision: Unix seconds (matching `ShotHistoryPlugin`)

The wire format and disk format both become **Unix seconds** stored as
unsigned 32-bit integers (`unsigned long` on ESP32). Web side switches its
`Date.now()` calls to `Math.floor(Date.now() / 1000)` and converts to
JS `Date` objects via `value * 1000`.

Rationale:
- Matches the existing `ShotHistoryPlugin` convention exactly.
- Web already does `Number(shot?.timestamp || 0) * 1000` for shots
  (`beanManager.js:368`) — same pattern applies to beans.
- Fits in `unsigned long` (32-bit) through the year 2106. The codebase
  already accepts this trade-off elsewhere; no regression.
- 1-second granularity is fine for human-edited bean records.

### Decision: NTP-not-synced sentinel

If `time(&now)` returns a year < 2020 (matching `AutoWakeupPlugin::isTimeValid()`),
treat as not-yet-synced. `createdAt` and `updatedAt` stay at `0`. The next
save once NTP has synced backfills both correctly because the existing
`if (bean.createdAt == 0)` guard already does the right thing.

Trade-off: a bean created and never edited again before its first NTP sync
keeps timestamp `0` indefinitely. Sort comparator already treats 0 as oldest,
so it sorts to the bottom — surprising but not corrupting. Acceptable for a
bean-inventory feature on an espresso machine that's almost always Wi-Fi
connected.

### Decision: Heuristic migration on read

Existing devices in the field have JSON files with garbage `millis()` values.
On `parseBean()` success, run:

```cpp
constexpr unsigned long LEGACY_TIMESTAMP_THRESHOLD = 1700000000UL;
if (bean.createdAt != 0 && bean.createdAt < LEGACY_TIMESTAMP_THRESHOLD) {
    bean.createdAt = 0;
}
if (bean.updatedAt != 0 && bean.updatedAt < LEGACY_TIMESTAMP_THRESHOLD) {
    bean.updatedAt = 0;
}
```

`1700000000` is 2023-11-14 — well after the project's first release. Any
value below that (and not zero) is provably a `millis()` artifact. The reset
is in-memory; the file on disk is corrected on next save.

Effect on existing users: bean sort scrambles for one boot post-update for
beans whose values get reset. Next user edit fixes it permanently. No
data loss; only timestamps are touched.

### Fix — three coordinated changes

#### 1. Firmware: `BeanManager::saveBean()`

In `src/display/core/BeanManager.cpp` around lines 78-83, replace:
```cpp
    const unsigned long now = millis();
    if (bean.createdAt == 0) {
        bean.createdAt = now;
    }
    bean.updatedAt = now;
```

With:
```cpp
    // Use NTP-derived Unix seconds (matches ShotHistoryPlugin convention).
    // If NTP has not synced yet, leave timestamps at 0; the next save will
    // backfill via the existing `bean.createdAt == 0` guard.
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    const bool ntpValid = timeinfo.tm_year > (2020 - 1900);
    const unsigned long nowSec = ntpValid ? static_cast<unsigned long>(now) : 0UL;

    if (bean.createdAt == 0 && nowSec != 0) {
        bean.createdAt = nowSec;
    }
    if (nowSec != 0) {
        bean.updatedAt = nowSec;
    }
```

The `<ctime>` header is needed; check for and add the include if not already
present in `BeanManager.cpp`.

#### 2. Firmware: legacy migration in `parseBean()` callers

`BeanManager::listBeans()` and `BeanManager::loadBean()` both call
`parseBean()`. The cleanest place to put the heuristic is inside `parseBean()`
itself (`BeanManager.h:25-48`) so both load paths benefit. Right before
the final `return !bean.name.isEmpty();` add:

```cpp
    // Migrate legacy millis()-based timestamps written by pre-CAR-102 firmware.
    // Any value below 2023-11-14 is too small to be a real Unix timestamp for
    // this project's lifetime — treat as corrupt and reset to 0.
    constexpr unsigned long LEGACY_TIMESTAMP_THRESHOLD = 1700000000UL;
    if (bean.createdAt != 0 && bean.createdAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.createdAt = 0;
    }
    if (bean.updatedAt != 0 && bean.updatedAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.updatedAt = 0;
    }
```

This is in the inline `parseBean` function in the header. The header is
included in both `BeanManager.cpp` and any consumer (currently only
BeanManager.cpp), so the constant has a single definition site.

#### 3. Web: `beanManager.js`

Three edits in `web/src/utils/beanManager.js`:

a. **Line 47** — `normalizeBeanPayload()` constructor:
```js
const now = Date.now();
```
becomes:
```js
const now = Math.floor(Date.now() / 1000);
```

b. **Lines 59-60** — already use `now`, no edit needed once (a) is in place.

c. **Around line 204** — `saveBean()` write:
```js
updatedAt: Date.now(),
```
becomes:
```js
updatedAt: Math.floor(Date.now() / 1000),
```

(If the same `saveBean` also writes `createdAt: Date.now()` in any path,
apply the same change. Verify during implementation.)

d. **Sort comparator at line 67** — unchanged. Direction is the same
regardless of unit.

e. **Future date-display code (out of scope for this PR)** — any UI that
formats `bean.updatedAt` for display must do `new Date(bean.updatedAt * 1000)`,
matching the existing pattern at `beanManager.js:368`.

### Risk

- **Existing devices** lose meaningful (already broken) sort order for one
  boot post-update on legacy beans. Acceptable per the migration design.
- **No-NTP devices** (no Wi-Fi ever, or NTP perma-blocked) cannot timestamp
  beans. Existing sentinel-0 sort behavior makes this a soft degradation.
- **2106 problem** in `unsigned long` (32-bit). Pre-existing in
  ShotHistoryPlugin; no regression here.
- **Concurrent save races** — pre-existing concern not introduced by this
  patch. Out of scope.

### Verification

- `pio run -e display` clean.
- `cd web && npm run build` clean.
- Manual:
  1. Boot device with NTP synced. Add a bean. Reboot. Confirm bean's
     `updatedAt` survives reboot and sorts correctly relative to any newly-
     added bean.
  2. Existing-device path: load a bean file containing legacy `millis()`
     values; confirm `parseBean()` resets them to 0 and the next save writes
     a real Unix timestamp.
  3. Fresh-boot-no-NTP path (best-effort, may need to test by disabling
     Wi-Fi pre-add): add a bean before NTP syncs. Confirm `createdAt = 0`
     persists. Connect Wi-Fi, edit the bean, confirm `createdAt` is
     backfilled to a real value.

---

## Cross-Cutting

### Linear workflow (per AGENTS.md)

Both issues already exist in `Backlog` with `Bug,firmware` labels. Workflow
identical to the previous cluster:

1. Move to **In Progress** when starting that PR.
2. Implement, then **QA**:
   - `pio run -e display`
   - `cd web && npm run build` (CAR-102 only)
3. Open PR targeting `dev-master` with `Fixes CAR-XXX`; move issue to
   **Ready for Testing**.
4. Self-review the diff; fix and re-push if anything is off.
5. **PR Ready** → merge → **Done**.

### PR shape

Two PRs, both against `dev-master`. Suggested titles:

- `fix(webui): use rollover-safe millis() comparisons (CAR-103)`
- `fix(beans): persist Unix-seconds timestamps; migrate legacy millis values (CAR-102)`

### Order of execution

1. **CAR-103** — mechanical, ~10 LOC, ships fast.
2. **CAR-102** — substantive, multi-file (firmware + web + migration),
   gets focused review.

Each is independent; order can change without blocking.
