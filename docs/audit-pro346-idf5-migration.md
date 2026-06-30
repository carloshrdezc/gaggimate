# IDF5 / Arduino-esp32 3.x migration firmware audit (PRO-346)

**Type:** SPIKE (code audit, docs-only deliverable). No functional/source changes were made under this issue.
**Tree audited:** `dev-master` @ `4a5439a9` (clone `~/work/gaggimate-pro346`, branch `carlos/PRO-346-firmware-audit-spike`).
**Scope:** GaggiMate firmware (`src/display/**`, `src/controller/**`, `lib/**`) + supporting web UI / tooling, with special emphasis on regressions/fragilities introduced or aggravated by the PRO-293 cluster (pioarduino Arduino-esp32 3.3.9 / IDF 5.5.4, C++20 flip, NimBLE 1.x->2.x, HomeSpan 1.x->2.x, ADC oneshot).

## Method

CI gates were reproduced locally and read critically (findings, not just pass/fail):

| Gate | Result |
|------|--------|
| `cd web && npm ci && npm run build` | PASS (built in 3.6s) |
| `pio test -e native` | PASS — 149/149 |
| `pio test -e native-sanitize` (ASan/UBSan) | PASS — 149/149, **zero sanitizer findings** |
| `pio run -e native -t compiledb` + `clang-tidy` | Ran clean (exit 0). All warnings are in vendored `lib/OTA/src/semver.c` (out of the gating `-f "+<src/...>"` scope); only nits in-scope (Rule-of-5 on RAII guard classes in `PluginManager.h`). |
| `pio run -e display` (full firmware link) | **Not run to completion in this audit environment** — requires downloading the ~GB pioarduino platform toolchain. Findings below are from static reading of the current tree + the host gates, not from a device/display link. On-hardware verification is N/A for a docs-only spike. |

The dominant technique was **fix-the-CLASS auditing**: take each merged PRO-293-cluster fix (the canonical reference being PRO-334, commit `57b73009`) and check SIBLING call paths for the same flaw that was fixed at only one site. PRO-334 established the pattern the rest of the stack is measured against:

1. Gate every internal-DMA / network allocation on `gmInternalLargestBlock()` (internal DRAM), **never** on `esp_get_free_heap_size()` (combined internal+PSRAM).
2. Drop-when-no-buffer below the floor (best-effort paths) or fail-fast clean (request paths).
3. Check `beginPacket()`/`endPacket()` returns; account failed sends.
4. Rate-limit any failure log that itself routes back through a log sink.
5. Report internal-DRAM headroom (not combined "free heap") as the health signal.

`HeapDiag.h` (PRO-334) is explicit that reporting/gating on the combined figure "is why the device could show ~6.9 MB free while a 512-byte sdmmc DMA buffer failed with ENOMEM," and names **"the legacy SSL relay guard"** as the cautionary example. Finding F1 is that exact guard, still present.

## Findings summary

| ID | Sev | Area | One-line | Migration-aggravated | Follow-up |
|----|-----|------|----------|----------------------|-----------|
| F1 | P1 | firmware | SSL cloud-relay startup gate uses combined heap, not internal DRAM | Yes (the named legacy anti-pattern PRO-334 replaced elsewhere) | bug / firmware |
| F2 | P2 | firmware | MQTTPlugin has no multi-fire idempotency guard; re-runs blocking connect + discovery per `controller:wifi:connect` | Yes (PRO-333 made the event fire repeatedly; mDNS got the guard, MQTT did not) | refactor / firmware |
| F3 | P2 | firmware | Boot-time ProfileManager SD reads stream `deserializeJson` off the handle with no internal-DRAM pre-flight | Yes (PRO-334 gate applied only to `loadProfile`, not its siblings) | refactor / firmware |
| F4 | P2 | firmware/web-ui | WebSocket reassembly buffer appends unbounded; the 64 KiB check only gates `reserve()` | Yes (real-display analog of the sim-only PRO-209 fix) | bug / firmware |
| F5 | P3 | ble | BLEScalePlugin uses `delay()` as cross-task callback-lifetime sync; unchecked `scanner` deref on one path | Yes (NimBLE 2.x changed callback dispatch) | refactor / ble |
| F6 | P3 | firmware | Combined "free heap" logged as relay health signal (observability) | Yes (same misleading metric as F1) | refactor / firmware |

Counts: **P1 ×1, P2 ×3, P3 ×2** (6 actionable findings → 6 follow-up issues).

---

## F1 (P1) — SSL cloud-relay startup gate reads combined heap, not internal DRAM

**Evidence:** `src/display/plugins/WebUIPlugin.cpp:866`

```cpp
// SSL heap usage can reach 50 KB; bail early rather than destabilize the device.
if (useSSL && esp_get_free_heap_size() < 60000) {
    ESP_LOGW("WebUIPlugin", "Insufficient heap (%u B) for SSL relay — skipping", ...);
    return;
}
...
relayWs.beginSSL(host.c_str(), port, path.c_str());   // :874
```

**Why it's a hazard (migration-aggravated):** `esp_get_free_heap_size()` returns the COMBINED internal+PSRAM pool, dominated by the multi-MB PSRAM region. The mbedTLS handshake this guard protects (`beginSSL`) draws its ~50 KB of in/out content buffers from the **internal** DMA-capable DRAM pool — the small pool that starves under HomeKit + BLE + WiFi + mDNS on Arduino-esp32 3.x / IDF 5.x. The guard can therefore pass (≥60 KB combined, almost always true thanks to PSRAM) while internal DRAM is exhausted, so `beginSSL` proceeds into an internal-pool starvation: the TLS handshake fails with `SSL - Memory allocation failed (-32512)`, and worse, the attempt keeps pressure on the very pool lwIP/mDNS/the async web server need — the PRO-334 web-UI-unreachable failure mode. This is the **identical** anti-pattern PRO-334 migrated the OTA-TLS check away from (`WebUIPlugin.cpp:414`, `gmInternalLargestBlock() >= kOtaCheckInternalDramFloorBytes`) and that `HeapDiag.h:6-12` names as "the legacy SSL relay guard." The sibling was never converted: fix-the-instance-not-the-class miss.

**Recommended fix direction:** Replace the `esp_get_free_heap_size() < 60000` gate with an internal-DRAM largest-block gate mirroring the OTA-TLS path — e.g. `if (useSSL && gmInternalLargestBlock() < kRelaySslInternalDramFloorBytes) { log + return; }`, with a floor sized for the mbedTLS handshake (the OTA path uses 48 KiB; size to the relay's measured handshake working set). Extract the constant into a small pure header (like `UdpLogTeePolicy.h` / `SdReadRetryPolicy.h`) so the decision is host-unit-testable in `[env:native]`. Add a `pio test -e native` case asserting the gate refuses below the floor and admits above it.

---

## F2 (P2) — MQTTPlugin re-runs a blocking connect + discovery on every `controller:wifi:connect`

**Evidence:** `src/display/plugins/MQTTPlugin.cpp:129-134` (handler), `:9-28` (`connect()` blocking loop), `:30-106` (`publishDiscovery`).

```cpp
void MQTTPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    pluginManager->on("controller:wifi:connect", [this, controller](const Event &) {
        if (!connect(controller))   // blocking: MQTT_CONNECTION_RETRIES * delay(MQTT_CONNECTION_DELAY)
            return;
        publishDiscovery(controller);
    });
```

**Why it's a hazard (migration-aggravated):** PRO-333 changed `controller:wifi:connect` from a once-per-session event into one that **fires repeatedly** — unconditional `STA_GOT_IP` handler + end-of-`setupWifi()` on first connect + the SoftAP-fallback watchdog re-arm on every recovery (`Controller.cpp:356-377`, `:375`). `mDNSPlugin` was explicitly hardened for exactly this with an `if (started) return;` idempotency guard, whose comment cites PRO-333 + the PRO-334 leak (`mDNSPlugin.cpp:20-26`). `MQTTPlugin` has **no** equivalent guard: each re-fire re-enters `connect()` — which runs up to `MQTT_CONNECTION_RETRIES` iterations of `client.connect()` separated by **blocking `delay(MQTT_CONNECTION_DELAY)`** — and then re-publishes the full HA discovery document. The handler runs on the `arduino_events` WiFi task, so the blocking retry loop stalls that task on every reconnect, and `client.begin()` (`:17`) is re-issued against an already-initialized client. This is the same multi-fire class mDNS was fixed for, left unfixed at the sibling site.

**Recommended fix direction:** Add the same idempotency discipline mDNS uses: skip `connect()` when `client.connected()` is already true, and only `publishDiscovery()` once per successful (re)connection. Move the blocking retry loop off the event-task hot path (latch "want MQTT" and let a periodic `loop()` tick drive a single non-blocking reconnect attempt), mirroring how WebUIPlugin defers work to the loop task. At minimum, guard against re-running discovery on a still-live connection.

---

## F3 (P2) — Boot-time ProfileManager SD reads bypass the PRO-334 internal-DRAM gate

**Evidence:** `src/display/core/ProfileManager.cpp`
- `collectProfileIdMigrations()` — `:39` `deserializeJson(doc, file)` streamed directly off the SD handle
- `listProfiles()` — `:122-123` same
- `findProfileIdBy*()` / setup scan — `:250-251` same

The bounded, gated path `readProfileFileBounded()` (`:308`, pre-flight `shouldAttemptSdRead(gmInternalLargestBlock())` + bounded retry + `kProfileMaxFileBytes` cap) is used **only** by `loadProfile()`. The enumeration/migration readers above were not converted.

**Why it's a hazard (migration-aggravated):** These readers all run at boot inside `ProfileManager::setup()` (`:380-396` → `remintUnsafeProfileIds` / `collectProfileIdMigrations` / `listProfiles`), which is precisely when internal DRAM is most contended — HomeSpan, BLE, WiFi and mDNS are all initializing. Each streams `deserializeJson` directly off an open SD `File`, the same pattern PRO-334's comment (`:277-282`) identifies as the ENOMEM-spin / WDT-reboot hazard. The blast radius is smaller than the `loadProfile` case (these run on the setup task, not the watchdog-petting async_tcp task, and over a directory of small files), so the realistic failure is a **degraded boot / missed id-migration / truncated profile list** under pressure rather than a guaranteed reboot — hence P2 not P1. But it is the same class fixed at one site only.

**Recommended fix direction:** Route the enumeration/migration readers through `readProfileFileBounded()` (or a shared bounded "read this profile file" helper) so the internal-DRAM pre-flight gate, the file-size cap, and the bounded read loop apply uniformly. Where a full parse of every file at boot isn't strictly needed, consider reading only the `id` field. Add a `[env:native]` test exercising the bounded helper on the enumeration path.

---

## F4 (P2) — WebSocket reassembly buffer grows unbounded (real display server)

**Evidence:** `src/display/plugins/WebUIPlugin.cpp:1182-1199`

```cpp
if (info->index == 0) {
    auto &buf = rxBuffers[cid];
    buf.clear();
    if (info->len <= 64 * 1024) {   // :1185 — gates ONLY the reserve()
        buf.reserve(info->len);
    }
}
auto &buf = rxBuffers[cid];
buf.append(reinterpret_cast<const char *>(data), len);   // :1191 — UNCONDITIONAL, no total cap
```

**Why it's a hazard (migration-aggravated):** The `info->len <= 64*1024` check guards only the optimistic `reserve()` — the actual accumulation at `:1191` appends every incoming fragment into the per-client `std::string` with **no cap on the reassembled total**. A client that declares a huge `info->len`, or simply streams many continuation frames, drives the `std::string` to grow without limit (from heap/PSRAM) until allocation fails — with no failure handling on the `append`. This is the real-display analog of the WebSocket frame-length 64-bit-overflow issue that was fixed **for the sim only** (PRO-209, commit `657bada6`); the brief explicitly asks to "check the real display web server too, not just sim." Mitigants that keep this at P2 rather than P1: the WS surface is LAN-local and the connection is configured `setCloseClientOnQueueFull(true)` (`:715`), and the disconnect path does erase the buffer (`:723`, so no per-disconnect leak). The unbounded reassembly itself remains.

**Recommended fix direction:** Enforce a hard cap on the reassembled message size against `info->len` AND the running `buf.size()` before each `append`; on exceed, drop the buffer and close/ignore the client (the JSON control messages this endpoint handles are all small — a few KB ceiling is ample). Factor the cap decision into a pure predicate testable in `[env:native]`, mirroring the sim's PRO-209 guard so both servers share the bound.

---

## F5 (P3) — BLE scale teardown uses `delay()` as cross-task callback-lifetime synchronization

**Evidence:** `src/display/plugins/BLEScalePlugin.cpp`
- `:33-34` "Give any running callbacks time to complete" → `delay(100)` before teardown
- `:256-264` `disconnect()`: `delay(50)` "to let any pending callbacks complete", then `scale = nullptr` (no `delete`)
- `:97` `scanner->stopAsyncScan();` dereferences `scanner` with no null check, though `scanner` can be null (`:76-78` `new (std::nothrow)` may return null and is logged-but-not-fatal)

**Why it's a hazard (migration-aggravated):** The scale object is torn down from the plugin's event handlers (which run on the `arduino_events` / AsyncTCP tasks) while NimBLE notify/connect callbacks fire on the **NimBLE host task**. The code relies on a fixed `delay(50/100)` to "let callbacks complete" — a timing assumption, not an ownership/lifetime guarantee. NimBLE-Arduino 2.x (PRO-290/PRO-330) changed callback dispatch and object ownership (`NimBLEScanCallbacks`, `onResult(const NimBLEAdvertisedDevice*)`), so a teardown racing a late callback is a genuine use-after-free / dangling-reference window that a sleep does not close. The `scanner` null-deref at `:97` is a latent crash on the OOM-scanner path. Kept at P3 because it works in practice (the delays usually suffice) and the sanitizer host gate can't exercise the real BLE task topology.

**Recommended fix direction:** Replace the timing-based teardown with a real handshake: stop the scanner / deregister callbacks, then either (a) tear down on the NimBLE callback task itself (the owner), or (b) use a flag + bounded wait that observes "no callback in flight" rather than a blind `delay`. Add the missing `if (scanner != nullptr)` guard at `:97` (the sibling call sites at `:210`, `:215`, `:247` already null-check). Clarify `scale` ownership (who `delete`s it) to rule out a leak.

---

## F6 (P3) — Combined "free heap" reported as relay health signal

**Evidence:** `src/display/plugins/WebUIPlugin.cpp:898`

```cpp
ESP_LOGI("WebUIPlugin", "Relay client started → %s:%d%s (free heap: %u B)", ...,
         static_cast<unsigned>(esp_get_free_heap_size()));
```

**Why it's a hazard (migration-aggravated):** Same misleading-metric root cause as F1, in the observability layer. Logging combined internal+PSRAM "free heap" as the relay's health figure is exactly the signal that misled diagnosis during PRO-334 (device showed ~6.9 MB free while a 512 B DMA alloc ENOMEM'd). An operator reading this log to judge whether the relay is memory-healthy gets the wrong answer on the dimension that actually matters (internal DRAM). Low severity (log line, no behavior change) but it perpetuates the metric PRO-334 set out to retire.

**Recommended fix direction:** Log internal-DRAM headroom alongside (or instead of) combined free heap at relay bring-up — reuse `GM_LOG_INTERNAL_DRAM("relay start")` from `HeapDiag.h`. Pairs naturally with F1.

---

## Follow-up issues filed (one per finding, Ref PRO-346)

Each finding above is filed as its own Linear issue in project **gaggimate**, team **PRO**, default state **Backlog**, with `Ref PRO-346`. Type label `bug` for defects/vulns, `refactor` for hardening; domain label `firmware` / `web-ui` / `ble`. See the PRO-346 comment for the issue IDs/links (filed after this report was committed). If issue creation was unavailable at run time, the verbatim title/body/labels for each are recorded in the PRO-346 comment so the orchestrator can file them.

## Notes / non-findings (audited, no action)

- **Display Controller WiFi ownership (PRO-333/335):** sound — single owner, handlers registered unconditionally before `WiFi.begin()`, watchdog re-arm on `STA_GOT_IP`.
- **WiFi/BLE coexistence ordering (PRO-330):** `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` is forced in `Controller::setupBluetooth()` before BT controller init, covering STA and AP — correct.
- **NVS init-order (PRO-331):** display settings are read in `setup()`, not the global ctor; the controller-board `GaggiMateController` global ctor takes only a version string and does all peripheral `new`/init in `setup()` — no global-ctor NVS read.
- **serveWebAsset path traversal:** the display server serves from a fixed in-memory asset table (`findWebAsset`), not the filesystem, so the PRO-208 `..` traversal class does not apply to it; `/api/history/` uses the library's `serveStatic` (its own traversal handling).
- **mDNS multi-start leak (PRO-334):** correctly guarded with `if (started) return;`.
- **Host gates:** `native` + `native-sanitize` both green with zero ASan/UBSan findings; clang-tidy in-scope clean (warnings confined to vendored `lib/OTA/src/semver.c`).
