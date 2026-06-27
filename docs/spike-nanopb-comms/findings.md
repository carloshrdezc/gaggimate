# PRO-239 — nanopb for the device↔display BLE protocol (SPIKE findings)

**Status:** SPIKE / throwaway prototype. Deliverable = numbers + go/no-go.
**Branch:** `pro-239-nanopb-spike` (off `dev-master`).
**Author:** coder profile, 2026-06-22.

---

## TL;DR

| Question | Answer |
|---|---|
| Round-trip lossless? | **YES** — `pb_encode → pb_decode` is bit-exact for all fields. The current text format is **lossy** (3-decimal `float_to_string`). |
| Flash delta (real `display` build) | **+7,776 bytes** (text +6,684, data +1,092). 0.12% of the 6.55 MB app partition. |
| RAM delta | **+0 bytes** (bss unchanged). nanopb confirmed **static-allocation only** (no malloc). |
| Wire size, sensor packet | text **45 B** → nanopb **25 B** → raw-binary floor **20 B**. Not the deciding factor. |
| nanopb runtime size | ~3.8 KB of directly-attributable symbols; 4,039 LOC of vendored C (encode/decode/common). |
| Migration cost | ~28 decode sites + 18 messages + a codegen pre-step. Mechanical but broad: ~1–1.5 days for a like-for-like swap. |
| **Recommendation** | **CONDITIONAL GO on Option A (roll our own nanopb layer on existing NimBLE chars), but NOT yet** — sequence it behind a decision about Option B. See §6. |

---

## 1. The `.proto` schema (`comms.proto`)

Drafted for **all 18 message types**, derived field-by-field from the fork's
actual encode/decode functions (`NimBLEServerController.cpp`,
`NimBLEClientController.cpp`) and the callback typedefs (`NimBLEComm.h:35-53`):

Controller→Display (NOTIFY): `SensorData` (5 float), `Error` (int), `BrewButton`
(bool), `SteamButton` (bool), `AutotuneResult` (4 float), `VolumetricMeasurement`
(float), `TofMeasurement` (int), `SystemInfo` (2 string + `Capabilities` 4 bool).

Display→Controller (WRITE): `SimpleOutput` (bool+2 float), `AdvancedOutput`
(bool+float+bool+2 float), `AltControl` (bool), `Ping` (empty), `PidSettings`
(4 float), `PumpModelCoeffs` (4 float, c/d may be NaN), `AutotuneRequest`
(2 int), `PressureScale` (float), `Tare` (empty), `LedControl` (2 uint).

The `OUTPUT_CONTROL` characteristic carries a `type` discriminator byte (0=simple,
1=advanced) → modeled as two messages, matching the two callbacks.

Generated max encoded sizes (from `comms.pb.h`): SensorData **25**, SimpleOutput
**12**, AdvancedOutput **19**, AutotuneResult/PidSettings/PumpModelCoeffs **20**,
SystemInfo (depends on string length).

**Note on `SystemInfo`:** today the `INFO` characteristic carries a *JSON string*
(`{"hw","v","cp":{"dm","ps","led","tof"}}`) that `Controller.cpp:234-253` parses
with ArduinoJson — i.e. there is already a second serialization layer on top of
the BLE characteristic. A nanopb migration would replace **both** the text/JSON
and the ArduinoJson parse for this message, which is a small bonus.

---

## 2. Working encode/decode prototype (lossless round-trip)

Built a **standalone g++ host harness** (`roundtrip_test.cpp` + `run.sh`),
**not** `pio test -e native`. Rationale (honest tradeoff for a spike): the native
env compiles ArduinoJson + production logic and would need the generator wired in
as a pre-step; a one-file g++ harness linked against the vendored nanopb runtime
answers the round-trip question with far less setup. Documented here so the choice
is explicit.

**Real nanopb** was used: generator `nanopb==0.4.9.1` (installed via `uv pip`),
runtime cloned from `github.com/nanopb/nanopb@0.4.9.1` and vendored under
`runtime/`. Generated `gen/comms.pb.{c,h}` with `nanopb_generator`.

Actual test output (`./run.sh`):

```
SensorData round-trip (5 floats):
  nanopb encoded length: 25 bytes (max-size define = 25)
  nanopb: all 5 fields bit-exact after round-trip.
  text format wire bytes: 45  -> "93.457001,9.123000,2.718000,3.142000,0.001000"
  text temperature: in=93.4567871 out=93.4570007  (delta=-0.0002136)
  text puck_resistance: in=0.0007654321 out=0.0010000000  (delta=-0.0002345680)

SimpleOutput round-trip (bool + 2 floats):
  nanopb encoded length: 12 bytes (max-size define = 12)
  nanopb: valve + both setpoints bit-exact after round-trip.

RESULT: PASS — round-trip lossless for both hot characteristics.
```

**Lossless: confirmed.** Every field compares bit-exact (`memcmp` on the IEEE-754
bits) after a full encode→bytes→decode cycle.

**The current format is measurably lossy.** `float_to_string` (`NimBLEComm.h:70`)
rounds to 3 decimals. The harness shows `puck_resistance = 0.0007654` → `0.001`
on the wire (a **~30% relative error** on a small sensor value), and temperature
loses ~0.0002 °C. Caveat surfaced by the test: the *simple_output* command path
uses `std::to_string(float)` (6 dp), so it is **not** the lossy path — the
**telemetry** sent through `float_to_string` is (`SensorData`, `AutotuneResult`,
`VolumetricMeasurement`, `PressureScale`). nanopb fixes both paths uniformly.

---

## 3. Measured footprint delta (the key number)

Measured against the **real `pio run -e display` firmware**, not a minimal
sketch. Method: build baseline `display`, then a throwaway footprint-measurement
env that links the nanopb runtime + the 2 generated messages via a force-linked
probe, and diff. (That throwaway env + its probe lib were removed in PRO-241 once
the number below was captured; this section preserves the measurement.)

PlatformIO "used" figures:

| Build | Flash (bytes) | RAM/static (bytes) |
|---|---|---|
| `display` (baseline) | 3,219,037 | 79,140 |
| `display-nanopb-spike` | 3,226,813 | 79,140 |
| **delta** | **+7,776** | **+0** |

`xtensa-esp32s3-elf-size` section breakdown (corroborates):

| | text | data | bss |
|---|---|---|---|
| baseline | 1,913,814 | 1,321,744 | 1,960,969 |
| +nanopb | 1,920,498 | 1,322,836 | 1,960,969 |
| **delta** | **+6,684** | **+1,092** | **+0** |

- **Flash: +7,776 bytes** (~7.6 KB) = **0.12%** of the 6.55 MB app partition. Of
  this, ~3.8 KB is directly-attributable nanopb symbols (`pb_encode`,
  `pb_decode`, `pb_common`, the 2 message descriptors, the probe); the remainder
  is section alignment/padding the linker adds.
- **RAM (bss): +0 bytes.** Message descriptors are `const` and live in flash
  (`.rodata`/data). nanopb encodes/decodes into caller-provided stack buffers.
- **Static allocation: VERIFIED, not assumed.** Every `malloc`/`realloc`/`free`
  in `pb_decode.c`/`pb.h` is gated behind `#ifdef PB_ENABLE_MALLOC`, which is
  **off by default**; with it off the runtime returns `"no malloc support"`
  errors rather than allocating. We did **not** define `PB_ENABLE_MALLOC`. The
  +0 bss delta is the empirical confirmation.

Extrapolation caveat: this measures the runtime + **2** messages. Adding all 18
adds more `const` descriptors (each ~24–48 B in flash, 0 in RAM) — call it
another ~1 KB flash, still ~0 RAM. So a full migration is on the order of
**~9 KB flash, ~0 RAM**. The runtime cost is paid once; per-message cost is tiny.

---

## 4. Wire-size comparison (sensor packet)

For representative values `93.457, 9.123, 2.718, 3.142, 0.000765`:

| Encoding | Bytes | Notes |
|---|---|---|
| current text (`float_to_string`+commas) | **45** | variable; grows with digits |
| nanopb | **25** | 5 × (1 tag byte + 4 fixed32) |
| raw binary floor | **20** | 5 × 4-byte float, no framing |

**Honest read:** nanopb roughly **halves** the sensor packet (45→25 B) and lands
near the raw-binary floor. But at the ~10 Hz notify rate over a BLE link with a
128-byte MTU, **wire size is not the deciding factor** — a single notify already
fits in one MTU either way, and the link is nowhere near saturated. Wire size is
a nice-to-have, not a reason to migrate. The real wins are **losslessness** and
**type-safety** (eliminating ~28 positional `get_token` parse sites).

---

## 5. Migration cost + own-roll vs absorb-upstream

### Scope of a like-for-like swap
- **18 messages** to define (done — `comms.proto`).
- **~28 decode sites** to convert from positional `get_token`/`toFloat`/`toInt`
  to typed field access: **19** in `NimBLEServerController.cpp`, **9** in
  `NimBLEClientController.cpp` (`grep -c get_token`).
- **~10 encode sites** (the `send*` methods on both controllers) to convert from
  string concat to `pb_encode`.
- Add a **codegen pre-step** to PlatformIO: a `pre:` extra_script running
  `nanopb_generator` on `comms.proto` into a generated dir, plus the
  `nanopb/Nanopb` lib dep (upstream's `NanoPbComm/library.json` already pins
  `nanopb/Nanopb@^0.4.9`). This is the one genuinely *new* build-system surface.
- Current `lib/NimBLEComm` is **932 LOC**. A nanopb rewrite of the same surface
  is comparable LOC but **mechanical** — the risk is breadth (28 sites), not
  depth. Estimate: **~1–1.5 days** for Option A, dominated by careful site-by-site
  conversion + on-device verification of every characteristic.

### Option A — roll our own nanopb layer on our existing NimBLE characteristics
- **Pros:** Minimal blast radius — keep all 18 UUIDs, the NimBLE
  server/client/scan/OTA plumbing, the `sim/comms` shim API, and the
  `Controller` call sites largely as-is. Only the *body* of each `send*`/decode
  changes (string concat → `pb_encode`; `get_token` → typed field). Cleanest
  diff; fully under our control; fixes the lossy-telemetry bug; reviewable in one
  PR per side (server/client).
- **Cons:** Diverges *further* from upstream's `NanoPbComm` — we'd own a
  bespoke nanopb layer that is neither the old text format nor upstream's v2.0.0
  architecture. Future upstream merges in the comms area get harder, not easier.

### Option B — absorb upstream's `NanoPbComm` (GM-82, lib **v2.0.0**)
Upstream's `lib/NanoPbComm` is **not a wire-format swap — it's a new comms
architecture**. Confirmed from upstream source:
- A single `Frame { uint32 id; uint32 ack; repeated Payload payloads }` with a
  `oneof Payload` tagged union of all messages — i.e. **one BLE characteristic**,
  not 18.
- A **CoalescingPriorityQueue** + **acknowledged outbound queue** (reliable,
  de-duplicated, coalesced delivery with id/ack bookkeeping).
- A **Transport abstraction** (`ble/` + `uart/` subdirs) — BLE today, UART
  planned.
- `Endpoint` / `Protocol` / `Messages` layering; ~10 source files.
- **What comes along:** adopting it means adopting the Frame/oneof protocol (a
  *breaking* wire change vs our current chars), the ACK/coalescing queue, the
  transport split, and — per the desktop-simulator divergence notes — it is
  entangled with upstream's NimBLE-server refactor and WiFi-coex changes. This is
  a multi-PR, multi-day effort that touches `Controller`, the sim, and the BLE
  init path, and it changes runtime behavior (coalescing, retransmit) that needs
  its own validation. It is the *direction of travel* but a large, risky bite.

### Simulator impact (`sim/comms/`)
The fork **does** have `sim/comms/` (CAR-399 desktop simulator, `[env:display-sim]`).
Important nuance: the sim's `NimBLEClientController` **mocks by value and never
serializes** — `sendOutputControl(...)` drives a local thermal model and fires
the registered callbacks with native C++ types directly (`MockController.cpp`).
So an **Option A** wire-format change is *mostly transparent to the sim*: as long
as the public method signatures (`sendOutputControl`, `registerSensorCallback`,
the callback typedefs in a shared header) are preserved, the sim shim needs
little-to-no change — it was never on the wire path. The 28-method shim surface
(`sim/comms/NimBLEClientController.h`) only needs touching if the migration
changes those **signatures** (e.g. passing a struct instead of loose floats).
**Option B**, by contrast, replaces the whole `NimBLEClientController` API with
`GaggiMateClient`/`Endpoint`, so the sim shim would need a full rewrite to match.

---

## 6. Recommendation

**CONDITIONAL GO — Option A, but explicitly sequenced behind a fork-strategy
decision; do NOT start coding yet.**

The spike validates the *technical* case cleanly: nanopb is lossless, costs
**~8 KB flash and 0 RAM** on the real display image, is static-allocation-only,
and the conversion is mechanical. The lossy-telemetry bug (`float_to_string`
3-dp rounding, demonstrably ~30% error on small `puck_resistance`) is a real
correctness reason to want this.

But the deciding axis is **fork-tracked vs divergent**, not the engineering:

- If the fork's strategy is to **converge with upstream** over time, then Option A
  is a *trap* — it spends ~1.5 days building a bespoke nanopb layer that upstream
  has already obsoleted with `NanoPbComm` v2.0.0, making the eventual Option-B
  merge *harder* (we'd be migrating text→our-nanopb→upstream-nanopb instead of
  text→upstream-nanopb). In that case: **NO-GO on A**, and instead schedule a
  separate, larger spike to absorb `NanoPbComm` (Option B) as a deliberate
  architecture adoption, accepting it drags in the NimBLE-server refactor +
  WiFi-coex + Frame/ACK/coalescing.

- If the fork's strategy is to **stay deliberately divergent** (own the comms
  layer, cherry-pick upstream features), then **GO on Option A**: it's the
  cleanest, lowest-risk way to kill the lossy text format, keeps the sim shim
  nearly untouched, and fits one reviewable PR per side.

**What would flip a NO-GO to GO:** (a) a decision that the fork is not tracking
upstream's comms architecture; or (b) the lossy-telemetry bug becoming
user-visible enough (e.g. shot-analysis artifacts from quantized
`puck_resistance`/flow) to justify the fix independently of the convergence
question — in which case Option A is the fast remedy and a future Option-B
adoption supersedes it.

**Suggested sequencing if GO (Option A):**
1. Land `comms.proto` + the codegen `pre:` step + `nanopb/Nanopb` dep (infra
   only, no behavior change). Pin the generated output in a test.
2. Migrate the **2 hot characteristics** first (SensorData, OUTPUT_CONTROL),
   verify on-device against the old format behind a build flag.
3. Migrate the remaining 16, one PR per controller side.
4. Keep the public `NimBLE*Controller` method signatures stable so `sim/comms`
   needs no rewrite.

---

## Artifacts in this directory

- `comms.proto` — the 18-message schema.
- `gen/comms.pb.{c,h}` — real nanopb-generated output (nanopb 0.4.9.1).
- `runtime/pb_*.{c,h}` — vendored nanopb runtime (from nanopb@0.4.9.1).
- `roundtrip_test.cpp` + `run.sh` — the lossless round-trip harness (PASS).
- The throwaway footprint-measurement build (a probe lib + an extra
  `platformio.ini` env) that produced the +7,776 B number above was **removed in
  PRO-241** when the production nanopb infra landed. It is intentionally gone;
  the measurement is preserved in §3.

### Reproduce
```sh
# round-trip test
cd docs/spike-nanopb-comms && ./run.sh
```

The footprint-delta reproduction relied on the throwaway measurement env, which
was removed in PRO-241 (see §3); the captured numbers above stand on their own.
