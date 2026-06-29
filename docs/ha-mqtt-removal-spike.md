# Spike: Removing the deprecated Home Assistant-over-MQTT integration (PRO-311)

Status: complete — **NO-GO on full removal.** Recommendation: **tidy via UI-collapse (alternative b)**.
Ref PRO-240. Spike deliverable per AGENTS.md: captured findings + go/no-go.

Branch/clone used for investigation: `carlos/PRO-311-spike-ha-mqtt-removal` off `origin/dev-master`
in a dedicated checkout (`~/work/gaggimate-pro311`). Upstream is `jniebuhr/gaggimate`
(added as remote `upstream`; the issue body's `gaggimate/gaggimate` URL 404s).

---

## Summary / go-no-go recommendation

**Do NOT fully remove the built-in MQTT plugin from this fork. Instead, do the small "tidy"
change: collapse the legacy HA-over-MQTT card in the web UI so it only shows when already
enabled, and leave all firmware behaviour intact.**

Three findings drive this:

1. **Upstream actively maintains the plugin** (`jniebuhr/gaggimate`). It is only *labelled*
   "(Deprecated)" in the UI — it has NOT been removed from `master`, and there is a live
   `upstream/feature/mqttEnhancements` branch that *expands* it. Deleting the plugin and its
   wiring in our fork would create recurring three-way merge conflicts on every upstream sync,
   across ~10 files, for zero functional benefit.
2. **The plugin is not HA-discovery-only** — it publishes a *generic* `gaggimate/<mac>/...`
   plain-topic state stream that is consumable by Node-RED / any MQTT dashboard, independent of
   Home Assistant. Removal would break legitimate non-HA MQTT consumers, not just HA users.
3. The blast radius is wide (firmware plugin + Controller + feature flag + platformio lib dep +
   headless excludes + 6 NVS keys + bounding constants + web card + form ingest/serialize +
   support-bundle redaction) and forces a settings-migration decision — all cost, no upstream
   alignment, since upstream keeps the feature.

If a future product decision *does* require removal (e.g. upstream finally deletes it), this doc
includes a ready-to-execute removal plan and migration decision in the appendix.

---

## Q1 — Upstream intent / timeline

**Upstream KEEPS the plugin; it is only relabelled "Deprecated", and is still being enhanced.**

- `git ls-tree upstream/master -- src/display/plugins/` lists both
  `src/display/plugins/MQTTPlugin.cpp` and `MQTTPlugin.h` present on `upstream/master` (HEAD).
- There is **no upstream removal commit**. Recent upstream commits *touch and improve* the plugin:
  - `bccb12c2 feat(display): route remaining JsonDocuments to PSRAM (GM-7) (#723)`
  - `a07e648d feat: Use ESP_LOG* functions for logging in MQTTPlugin. (#565)`
  - `093d37d8 feat: add mqtt auto discovery (#220)`
- A **live upstream feature branch `upstream/feature/mqttEnhancements`** exists and carries a
  *different* `MQTTPlugin.cpp` blob (`c03f6082…`) than `upstream/master` (`848c019f…`) — i.e.
  upstream is investing in MQTT, not retiring it.
- The "(Deprecated)" wording lives only in the web UI card on upstream master
  (`upstream/master:web/src/pages/Settings/PluginCard.jsx:273` — "Home Assistant over MQTT
  (Deprecated)"), pointing users to `https://github.com/gaggimate/ha-integration`. Deprecation
  is a *documentation/UX nudge*, not a code removal.

**Merge-conflict cost if we remove it in our fork.** Our `dev-master` already diverges from
`upstream/master` on most of these files (the fork has its own June UI rework, headless work,
topic-hardening, etc.), so they are *already* manual-merge candidates. The incremental risk is
that **deleting the MQTT lines makes those merges actively conflict** whenever upstream edits
the same hunks. The files where upstream still edits MQTT/HA code and we would have deleted it:

| File | Upstream still edits MQTT here? | Conflict risk on removal |
|------|-------------------------------|--------------------------|
| `src/display/plugins/MQTTPlugin.cpp` | Yes (PSRAM #723, ESP_LOG #565, mqttEnhancements branch) | High — file deleted vs upstream-modified = delete/modify conflict every sync |
| `src/display/plugins/MQTTPlugin.h` | Yes (mqttEnhancements blob differs) | High — same delete/modify pattern |
| `src/display/core/Controller.cpp` | Plugin registration block | Medium — region near `registerPlugin(new MQTTPlugin())` |
| `web/src/pages/Settings/PluginCard.jsx` | Yes (card text/links) | Medium |
| `src/display/core/Settings.{h,cpp}` | HA fields/NVS keys | Medium |
| `platformio.ini` | `256dpi/MQTT` lib dep line | Low-Medium |

Verified with `git diff --quiet upstream/master origin/dev-master -- <file>`: only
`MQTTPlugin.h` is currently byte-identical to upstream; every other listed file already differs,
confirming these are exactly the hunks where future syncs already need care.

**Conclusion: removing in-fork = permanent, recurring merge tax against an actively-maintained
upstream feature. Strong signal toward NO-GO.**

---

## Q2 — Non-HA MQTT users

**The plugin is NOT HA-discovery-only. It publishes a generic, plain-topic state stream that
any MQTT consumer (Node-RED, dashboards, custom scripts) can use without Home Assistant.**

Evidence in `src/display/plugins/MQTTPlugin.cpp`:

- The generic publisher builds plain topics under a fixed `gaggimate/<mac>/...` namespace —
  **not** the HA discovery namespace:
  - `publish()` — `MQTTPlugin.cpp:108-121`: `snprintf(publishTopic, …, "gaggimate/%s/%s", cmac, topic.c_str())`.
- Live state is pushed to these plain topics regardless of HA:
  - Boiler temperature → `gaggimate/<mac>/boilers/0/temperature` (`MQTTPlugin.cpp:136-146`)
  - Boiler target temp → `gaggimate/<mac>/boilers/0/targetTemperature` (`MQTTPlugin.cpp:147-154`)
  - Mode → `gaggimate/<mac>/controller/mode` with `{"mode":N,"mode_str":"…"}` (`MQTTPlugin.cpp:155-181`)
  - Brew state → `gaggimate/<mac>/controller/brew/state` with `{"state":…,"timestamp":…}` (`MQTTPlugin.cpp:122-127, 182-184`)
- The **only** HA-specific surface is the one-shot auto-discovery config publish in
  `publishDiscovery()` (`MQTTPlugin.cpp:30-106`), which emits to
  `<haTopic>/device/<mac>/config` (`MQTTPlugin.cpp:98-99`). This is *additive* — a non-HA
  consumer simply ignores the discovery message and subscribes to the plain `gaggimate/<mac>/#`
  topics above.
- The connection itself is a plain MQTT broker connection (`client.begin(ip, haPort, net)`,
  `MQTTPlugin.cpp:17`), default port 1883 — i.e. any broker, not an HA-only endpoint.

**Conclusion: full removal would break legitimate non-HA MQTT integrations, not merely the
deprecated HA path. The card's "Home Assistant" framing understates what the firmware actually
does. Another point toward NO-GO.**

---

## Q3 — Settings migration story (if removed)

Six persisted NVS keys exist (Preferences):
`ha_a` (bool), `ha_i`, `ha_p` (int), `ha_t`, `ha_u`, `ha_pw`
(load: `Settings.cpp:74-83`; save: `Settings.cpp:634-639`; accessors `Settings.h:90-95, 167-172`;
backing fields `Settings.h:239-244`; bounding constants
`DEFAULT_HOME_ASSISTANT_TOPIC` / `MAX_HOME_ASSISTANT_TOPIC_LENGTH` at `constants.h:24,29`).

Options if the keys' code were removed:

- **(a) Strip on next save** — actively `preferences.remove("ha_*")` once. Cleanest NVS, but
  needs throwaway migration code that itself must be removed later; risk of clobbering data if a
  future feature reuses a key name.
- **(b) Leave as orphaned keys** — stop reading/writing them; the bytes sit dormant in NVS. Zero
  migration code. Downsides: a few hundred bytes of dead NVS per device, and the values
  (including `ha_pw`) linger unencrypted. Low risk, effectively invisible to users.
- **(c) One-time migration** — version-gated read+delete. Most "correct", most code, highest
  test burden for a deprecated feature.

**Recommendation (conditional on removal ever happening): (b) leave as orphaned keys.** ESP32
Preferences/NVS tolerates orphaned keys indefinitely; the cost is negligible and it avoids
shipping migration code for a feature nobody should be re-enabling. The web form would simply
drop the fields (Q4), so users can't write new values. **However, since the overall
recommendation is NO-GO, no migration is performed at all** — the keys remain fully live.

---

## Q4 — Removal blast radius (every file/symbol/flag)

Full enumeration (for the appendix removal plan; current recommendation does NOT execute this):

Firmware:
- `src/display/plugins/MQTTPlugin.cpp` — delete file. Symbols: `MQTTPlugin::connect`,
  `publishDiscovery`, `publish`, `publishBrewState`, `setup`; const `MQTT_CONNECTION_RETRIES`,
  `MQTT_CONNECTION_DELAY`.
- `src/display/plugins/MQTTPlugin.h` — delete file. Class `MQTTPlugin`, members `client`
  (`MQTTClient`), `net` (`WiFiClient`), `lastTemperature`.
- `src/display/core/Controller.cpp` — remove `#if GAGGIMATE_ENABLE_MQTT` / `#include
  <display/plugins/MQTTPlugin.h>` (L33-34) and the registration block
  `if (settings.isHomeAssistant()) registerPlugin(new MQTTPlugin())` (L117-121).
- `src/display/config/features.h` — remove `GAGGIMATE_ENABLE_MQTT` define block (L26-28).
- `platformio.ini` — remove lib dep `256dpi/MQTT@2.5.3` (L71); remove headless exclude
  `-<display/plugins/MQTTPlugin.cpp>` (L336); remove `-DGAGGIMATE_ENABLE_MQTT=0` in the
  native env (L141) and the headless env (L366).
- `src/display/core/Settings.h` — remove accessors `isHomeAssistant`/`getHomeAssistant*`
  (L90-95), setters (L167-172), fields (L239-244).
- `src/display/core/Settings.cpp` — remove load (L74-83, incl. the topic self-heal clamp),
  setters (L340-369), save (L634-639).
- `src/display/core/constants.h` — remove `DEFAULT_HOME_ASSISTANT_TOPIC` (L24) and
  `MAX_HOME_ASSISTANT_TOPIC_LENGTH` (L25-29 incl. comment).
- `src/display/plugins/WebUIPlugin.cpp` — remove form ingest of `homeAssistant`/`haUser`/
  `haPassword`/`haIP`/`haPort`/`haTopic` (L1484-1494) and the `/api/settings` serialization of
  `homeAssistant`/`haUser`/`haPassword`/`haIP`/`haPort`/`haTopic` (L1605-1610).
  - **`haPassword` is a sentinel round-trip, not a plain serialized value.** The `/api/settings`
    serialization emits the `kSecretSentinel` placeholder (`"---unchanged---"`,
    `WebUIPlugin.cpp:36`) for `haPassword` (`WebUIPlugin.cpp:1607`) rather than the raw secret, and
    the form-ingest path skips the write when the incoming value equals that sentinel
    (`WebUIPlugin.cpp:1487-1488`), preserving the stored password on round-trip. An implementer
    executing the conditional full removal must handle `haPassword` as a sentinel round-trip (drop
    both the sentinel-emit and the sentinel-guarded ingest), not as a plain serialized field.

Web UI:
- `web/src/pages/Settings/PluginCard.jsx` — remove the entire "Home Assistant over MQTT
  (Deprecated)" card (L278-399), including toggle + IP/port/user/password/topic inputs and the
  `ha-integration` link.
- `web/src/pages/Settings/index.jsx` — remove the `homeAssistant` toggle handler (L69-70); audit
  `formData` defaults for `haIP/haPort/haUser/haPassword/haTopic` (set via spread of the
  `/api/settings` response — once the firmware stops serializing them they vanish, but verify no
  hardcoded defaults remain).
- `web/src/pages/OTA/index.jsx` — remove `delete data.haPassword;` (L456) support-bundle
  redaction (harmless to leave, but it references a then-nonexistent field).

**256dpi/MQTT dependency usage check:** grep of the whole `src/` tree for `MQTT.h` /
`MQTTClient` / `256dpi` returns matches **only** in `src/display/plugins/MQTTPlugin.h`
(`#include <MQTT.h>` L4, `MQTTClient client;` L22). Nothing else uses the library — so removing
the plugin lets us safely drop the `256dpi/MQTT@2.5.3` lib dep.

Build-flag note: live platformio pin is **`256dpi/MQTT@2.5.3`** (`platformio.ini:71`) — confirms
the issue body's "2.5.2" was stale; it is `2.5.3`.

---

## Q5 — Alternatives compared + recommendation

- **(a) Full removal** — removes a *generic* MQTT feature that upstream still maintains and that
  non-HA users rely on (Q2); ~10 files; forces a migration decision (Q3); and incurs a recurring
  merge tax vs upstream (Q1). High cost, breaks users, no upstream alignment. **Reject.**
- **(b) Hide/collapse the legacy card unless already enabled** — web-only change: render the
  HA-over-MQTT card (or its expanded body) only when `formData.homeAssistant` is already true.
  New users never see the deprecated option; existing users keep working firmware untouched;
  zero firmware/NVS/lib changes; minimal upstream-merge surface (one web file). **Lowest risk,
  achieves the deprecation intent (stop new adoption) without breaking anyone.**
- **(c) Leave as-is** — already shipping with a "(Deprecated)" label and a link to the external
  `ha-integration`. Acceptable, but new users can still freshly enable a deprecated path.

**Recommendation: (b) collapse/hide the legacy card for new users.** It satisfies the
deprecation goal (discourage new adoption) at near-zero blast radius and zero merge tax, while
preserving the generic MQTT stream that non-HA users depend on.

### Scoped "tidy" change (recommended, ready to hand to a coder)

Single file: `web/src/pages/Settings/PluginCard.jsx`.

- Today the card header + toggle always render, and the body renders only when
  `formData.homeAssistant` is truthy (`PluginCard.jsx:294`). Change so the **entire card**
  (L278-399) is gated on the feature already being enabled, e.g. wrap in
  `{formData.homeAssistant && ( …card… )}`. Result: a device that already has HA-over-MQTT on
  keeps seeing/editing it; a device that has it off never sees the deprecated card at all.
- Optionally add a short "deprecated — see [Home Assistant Integration](…/ha-integration)" note
  near the existing integrations section so the documentation breadcrumb survives.
- No firmware change. `WebUIPlugin.cpp` keeps ingesting/serializing the fields, so already-enabled
  devices remain fully functional. No NVS migration. No `platformio.ini` / lib change. New Linear
  issue (type `chore`/`refactor`, label `web`) for the implementation.

No-go rationale recap: upstream maintains it (Q1), it serves non-HA consumers (Q2), and the full
removal cost/migration burden (Q3/Q4) is unjustified for a feature that is merely soft-deprecated.

---

## Appendix — concrete removal plan (only if a future decision mandates full removal)

If product later decides to remove (e.g. upstream itself deletes the plugin), execute the Q4
blast-radius list in this order, then:

- Migration decision: **(b) leave the 6 `ha_*` NVS keys orphaned** (no migration code).
- Drop lib dep `256dpi/MQTT@2.5.3` (safe — sole consumer is the deleted plugin, per Q4 grep).
- Build gates to pass before PR (per AGENTS.md CI): `cd web && npm ci && npm run build`;
  `pio run -e display`; `pio run -e display-headless`; `pio test -e native` +
  `pio test -e native-sanitize`; `pio check -e display` and `-e controller`; clang-tidy.
- Expect to re-resolve the delete/modify conflicts against `upstream/master` on the next sync
  (Q1) — document the deletion in the sync runbook so future merges `git rm` the plugin again.
