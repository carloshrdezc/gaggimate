# PRO-513 Gaggimate App Architecture, Security, UX, and Performance Audit

## Executive summary

This audit reviewed the firmware display/controller surfaces, cloud relay, and web UI code paths that expose machine control, settings persistence, shot data, and browser-local storage.

Top risks:

1. The local HTTP and WebSocket API has no authentication or authorization and also emits permissive CORS headers. Any device or web page that can reach the machine on the local network can read settings, change settings, start/stop processes, change modes, trigger OTA/autotune flows, and access diagnostic/core-dump endpoints.
2. Remote relay tokens are persisted in browser `localStorage` and transmitted as WebSocket query parameters by both browser and firmware. This makes bearer tokens easier to leak through browser storage, URL history, intermediary logs, and server-side request logs.
3. Visualizer.coffee credentials can be stored as plaintext passwords in `localStorage`; the UI copy says only the username is remembered, which creates a misleading security affordance.
4. Firmware settings writes have improved String/vector snapshotting, but scalar fields and the `dirty` flag are still updated from web/WS tasks and read by the deferred settings task without a single coherent settings transaction lock. This can persist mixed snapshots during concurrent saves.
5. Several user-supplied firmware settings and WebSocket request payloads are parsed with permissive conversions or minimal shape validation. Bad inputs usually fail softly, but can still persist unsafe values, mislead the UI, or make troubleshooting hard.

No hardcoded production API keys, private keys, or credential files were found in the audited source tree. Secrets are stored at runtime in NVS or browser storage, not committed as literal production credentials.

## Methodology

Reviewed current `dev-master` head `20b73b33 refactor(firmware): guard scale->isConnected() TOCTOU in scan() (PRO-512) (#512)` from a fresh clone at `/home/carlos/work/gaggimate-pro513`.

Files/subsystems reviewed:

- Firmware event architecture: `src/display/core/PluginManager.h`, `src/display/core/PluginManager.cpp`, `src/display/core/Event.h`, `src/display/core/EventIds.h`.
- Firmware settings/persistence: `src/display/core/Settings.h`, `src/display/core/Settings.cpp`, settings handlers in `src/display/plugins/WebUIPlugin.cpp`.
- Firmware network/API surface: `src/display/plugins/WebUIPlugin.cpp`, `src/display/plugins/WebUIPlugin.h`, `PathTraversalPolicy.h`, WebSocket reassembly and broadcast policy headers.
- BLE scale path: `src/display/plugins/BLEScalePlugin.cpp`, `src/display/plugins/BLEScalePlugin.h`, `lib/NimBLEComm/src/NimBLEClientController.cpp`, HTTP scale endpoints.
- Controller surface: `src/controller/main.cpp` and display/controller integration call sites found by search.
- Web UI services and pages: `web/src/services/ApiService.js`, `web/src/pages/Home/DashboardMerged.jsx`, `web/src/pages/ShotHistory/index.jsx`, `web/src/pages/ShotAnalyzer/index.jsx`, `web/src/pages/ShotAnalyzer/components/LibraryPanel.jsx`, `web/src/pages/Settings/index.jsx`, `web/src/components/Navigation.jsx`, `web/src/components/VisualizerUploadModal.jsx`, `web/src/utils/download.js`, `web/src/pages/Home/dashboardLogic.js`, IndexedDB and library services.
- Relay server: `relay-server/src/index.js`, `relay-server/src/relay.js`, top-level relay server references.
- Repo-wide sweeps: credential filename sweep and token/secret/password/private-key grep across `src`, `lib`, `web`, `relay-server`, `data`, and `scripts`.

Limitations:

- Source audit only. No live ESP32 hardware, BLE scale, browser accessibility tree, network scanner, or manual UX session was exercised for this report.
- Findings marked as needing hardware/manual validation should be verified on-device or in a browser before final severity is frozen.

## Findings

### [High] Local API exposes machine control and sensitive device data without authentication

- **Location**: `src/display/plugins/WebUIPlugin.cpp:85-89`, `src/display/plugins/WebUIPlugin.cpp:872-895`, `src/display/plugins/WebUIPlugin.cpp:911-923`, `src/display/plugins/WebUIPlugin.cpp:1304-1501`
- **Description**: The web server registers sensitive endpoints for `/api/settings`, `/api/status`, BLE scale management, shot history, core dumps, diagnostic log downloads, and WebSocket process/mode/OTA/autotune handlers without an authentication check. The same server adds `Access-Control-Allow-Origin: *` and allows `GET, POST, OPTIONS`, so a malicious page opened by a user on the same LAN can issue HTTP requests to the machine if it can resolve/reach the device. WebSocket handlers accept process commands such as `req:process:activate`, `req:process:deactivate`, `req:change-mode`, `req:ota-start`, and `req:autotune-start` directly after JSON parsing. Impact: local-network CSRF/control risk, settings tampering, process interruption/start, OTA misuse, and leakage of diagnostics/core dumps.
- **Remediation**: Add a device-local auth model before sensitive HTTP and WebSocket routes. Prefer a setup-generated random admin token stored in NVS, sent as an `Authorization` header for HTTP and as an authenticated WebSocket handshake/subprotocol for WS. Restrict CORS to the device origin by default; only enable wildcard CORS in explicit development/AP captive-portal flows. Require CSRF-resistant semantics for settings writes and dangerous process/OTA actions.

### [Medium] Cloud relay bearer token is stored and transmitted in URL/query surfaces

- **Location**: `web/src/services/ApiService.js:82-100`, `src/display/plugins/WebUIPlugin.cpp:1131-1165`, `relay-server/src/index.js:51-63`, `relay-server/src/relay.js:18-30`
- **Description**: The browser accepts `relay` and `token` URL parameters, stores both in `localStorage`, and connects to `${relayUrl}/connect?token=...&role=browser`. Firmware similarly builds `/connect?token=...&role=device`. The Cloudflare Worker routes sessions using the query token as the Durable Object name. Query-string bearer tokens are commonly captured in browser history, copied URLs, proxy/load-balancer logs, server request logs, and support screenshots. `localStorage` persistence also exposes the token to any future XSS in this origin. Impact: stolen relay token grants remote browser/device session access for that machine.
- **Remediation**: Move relay auth out of query strings. Use a one-time setup code to provision a long-lived token, then send it in a WebSocket subprotocol or authorization frame after connection establishment. Store browser-side relay secrets in a less persistent scoped store where possible, support rotation/revocation, and validate relay origins/protocols before persisting them.

### [Medium] Visualizer.coffee password can be remembered as plaintext browser storage while copy says only username is remembered

- **Location**: `web/src/components/VisualizerUploadModal.jsx:31-39`, `web/src/components/VisualizerUploadModal.jsx:60-72`, `web/src/components/VisualizerUploadModal.jsx:212-215`
- **Description**: When `Remember credentials` is checked, the modal stores both `visualizer_username` and `visualizer_password` directly in `localStorage`. On subsequent opens it repopulates the password field from storage. The explanatory copy says credentials are stored locally only if the user chooses to remember the username, not the password. Impact: browser-local plaintext password exposure, especially on shared machines, backups, browser profile sync, XSS, or shoulder-surfing after auto-fill; misleading UX makes users underestimate the security tradeoff.
- **Remediation**: Do not persist the password. Remember only username, or use a scoped access token/OAuth-style upload token if Visualizer.coffee supports it. Update copy to state exactly what is stored. If password persistence remains, require explicit text such as “Remember username and password on this browser” and provide a clear “forget credentials” action.

### [Medium] Settings persistence still lacks a coherent transaction lock for scalar fields and the dirty flag

- **Location**: `src/display/core/Settings.cpp:256-260`, `src/display/core/Settings.cpp:854-918`, `src/display/core/Settings.cpp:919-1008`, `src/display/core/Settings.cpp:1013-1017`, `src/display/plugins/WebUIPlugin.cpp:1850-1992`
- **Description**: `Settings::doSave()` now snapshots all String fields under `selectedNameMutex` and vectors under `vectorMutex`, which mitigates the prior torn-String/vector hazards. However, scalar fields such as temperatures, booleans, mode flags, numeric timing values, and `dirty` itself are still written by HTTP/WS/event-task setters while the deferred settings task periodically calls `doSave()` and writes those fields to NVS. `handleSettings()` performs a large batch update and calls `save(true)`, but the 5-second settings task can also observe `dirty` mid-batch and persist a mixed old/new scalar snapshot. Impact: rare but user-visible settings inconsistency after concurrent settings changes, especially for multi-field forms where a partial save can combine fields from different UI submissions.
- **Remediation**: Introduce one settings-state mutex (or a two-phase immutable snapshot object) that covers all persisted fields and `dirty` transitions. `batchUpdate()` should hold the state lock across the batch mutation and create a complete save snapshot before writing NVS. Keep flash writes outside long-held locks if necessary by copying every persisted field into a snapshot under the lock, including scalars and schedules.

### [Medium] Settings POST accepts many unsafe or malformed values through permissive numeric parsing

- **Location**: `src/display/plugins/WebUIPlugin.cpp:1852-1938`, `src/display/plugins/WebUIPlugin.cpp:1941-1988`
- **Description**: The `/api/settings` POST handler uses `request->arg(...).toInt()`, `toFloat()`, and `toDouble()` across many fields. Arduino `String` numeric conversion silently returns `0` on malformed strings and accepts partially numeric strings. Some setters clamp later, but others persist or apply raw values. The schedule parser also accepts arbitrary time strings when the `|days` format is present. Impact: malformed HTTP form data can silently reset settings to zero, push odd values into control/persistence, or make the UI display a saved setting that the user never intentionally selected.
- **Remediation**: Add per-field validation before calling setters. Reject malformed numbers with HTTP 400 and a JSON error, clamp only after confirming a parse consumed the full string, and validate enum/range/time formats explicitly. Consider a shared `parseIntStrict`, `parseFloatStrict`, and schedule parser with tests.

### [Medium] WebSocket message validation checks only top-level shape before dispatching powerful commands

- **Location**: `web/src/services/ApiService.js:290-324`, `src/display/plugins/WebUIPlugin.cpp:1304-1314`, `src/display/plugins/WebUIPlugin.cpp:1325-1501`
- **Description**: The browser discards malformed JSON and requires a truthy `tp`, but otherwise trusts message fields and maps them into `machine.value`. Firmware dispatch similarly deserializes JSON and branches on `tp`, with each handler doing ad hoc validation. Several process commands have no request origin/auth distinction. Impact: compromised relay/browser/local client can send unexpected field types or partial payloads that produce UI inconsistencies or trigger device actions; defensive validation is uneven and hard to reason about.
- **Remediation**: Define an explicit message schema for every `req:*`, `res:*`, and `evt:*` frame. Validate frame type and payload before side effects; reject unknown fields or wrong types for command frames. Generate shared TypeScript/C++ schema documentation or tests from the message contract.

### [Low] BLE scale HTTP connect endpoint reports success even when input is missing or connection is not established

- **Location**: `src/display/plugins/WebUIPlugin.cpp:2114-2125`, `src/display/plugins/BLEScalePlugin.cpp:260-273`, `src/display/plugins/BLEScalePlugin.cpp:430-461`
- **Description**: `/api/scales/connect` requires POST but then calls `BLEScales.connect(request->arg("uuid").c_str())` and always returns `{success: true}`. `BLEScalePlugin::connect()` rejects an empty UUID, and `establishConnection()` can later fail if the device is not found or connect fails, but the HTTP response still says success. Impact: the web UI can show optimistic success even though no scale was selected/connected, making BLE setup harder to troubleshoot.
- **Remediation**: Validate that `uuid` is present and matches a discovered device before returning success. Return `{success:false,error:"..."}` or HTTP 400 for missing/invalid UUID and only report success for an accepted connection attempt. Add a follow-up status poll or event to report actual connection completion/failure.

### [Low] Remote relay URL provisioning accepts arbitrary persisted relay origins

- **Location**: `web/src/services/ApiService.js:82-100`, `web/src/pages/Settings/index.jsx:192-214`, `src/display/plugins/WebUIPlugin.cpp:1087-1093`
- **Description**: A URL containing `?relay=...&token=...` causes the browser to persist an arbitrary relay URL and token. Firmware also accepts a configured relay URL if it parses as `ws://` or `wss://`. This may be intentional for self-hosted relay support, but there is no warning when the browser/device is pointed at a non-default relay. Impact: phishing or support links can silently pin a browser to an attacker-controlled relay until localStorage is cleared or settings are changed.
- **Remediation**: When a relay URL is first provisioned or changed, show the target host clearly and require explicit confirmation. Prefer `wss://` by default, warn on `ws://`, and consider an allow-list or “advanced/self-hosted relay” mode for arbitrary relay hosts.

### [Low] Navigation drawer lacks modal semantics and focus trapping

- **Location**: `web/src/components/Navigation.jsx:87-98`, `web/src/components/Navigation.jsx:70-83`, `web/src/components/Navigation.jsx:107-114`
- **Description**: The mobile navigation drawer responds to Escape and has a close button, but the drawer is rendered as an `aside` with `aria-hidden` instead of modal/dialog semantics, and focus is not trapped while open. The backdrop is a button and can be focusable when open, but keyboard users can still tab into page content behind the drawer. Impact: accessibility friction for keyboard and screen-reader users.
- **Remediation**: Treat the mobile drawer as a modal navigation dialog: add `role="dialog"` or equivalent semantics, `aria-modal="true"` while open, focus the first/close item on open, restore focus on close, and trap Tab/Shift+Tab within the drawer.

### [Low] Analyzer library sticky bar updates layout state on every scroll event

- **Location**: `web/src/pages/ShotAnalyzer/components/LibraryPanel.jsx:187-205`
- **Description**: `LibraryPanel` registers `scroll` and `resize` listeners that call `getBoundingClientRect()` and `setBarRect(...)` on each event. A ResizeObserver also updates dimensions. On scroll-heavy analyzer sessions this can force layout and component state updates more often than needed. Impact: potential jank on lower-powered mobile browsers, especially with large shot libraries/charts.
- **Remediation**: Throttle scroll/resize updates with `requestAnimationFrame`, only set state when width/left/height materially changed, and rely on CSS sticky where possible. Keep ResizeObserver for true size changes.

### [Low] Clean finding: plugin event bus has strong concurrency hardening

- **Location**: `src/display/core/PluginManager.h:22-73`, `src/display/core/PluginManager.cpp:99-181`, `src/display/core/EventIds.h:19-88`
- **Description**: The event bus uses a mutex-protected copy-on-write listener map and releases the lock before invoking callbacks. Missing-key triggers are read-only, event IDs are centralized as `EventIds::*` constants, and dispatch stops on `event.stopPropagation`. This reduces prior typo, map mutation, and callback reentrancy hazards. Impact: plugin architecture is in comparatively good shape for current firmware concurrency.
- **Remediation**: Continue moving literal event IDs to `EventIds`. Add optional payload contract helpers if event payload types grow beyond simple int/float/String entries.

### [Low] Clean finding: WebSocket reassembly and BLE scale lifetime have important recent safeguards

- **Location**: `src/display/plugins/WebUIPlugin.cpp:1504-1543`, `src/display/plugins/BLEScalePlugin.cpp:295-345`, `src/display/plugins/BLEScalePlugin.cpp:396-428`, `src/display/plugins/BLEScalePlugin.cpp:470-536`
- **Description**: WebSocket message reassembly is capped and closes oversize clients. BLE scale teardown uses atomic single-owner claims, mutex-guarded `scale` accessors, and a bounded callback-drain flag. These are meaningful protections against heap growth and cross-task use-after-free classes. Impact: no immediate follow-up needed for these specific previously-risky paths beyond hardware stress validation.
- **Remediation**: Preserve these invariants when adding scale drivers or WS message types. Run native policy tests and on-device BLE disconnect/reconnect stress tests after touching this subsystem.

### [Low] Clean finding: no committed production secrets were found in the audited source sweep

- **Location**: repo-wide secret sweep; representative runtime secret storage in `src/display/core/Settings.cpp:67-68`, `src/display/core/Settings.cpp:104-105`, `src/display/core/Settings.cpp:192-194`, `src/display/plugins/WebUIPlugin.cpp:2019-2022`, `src/display/plugins/WebUIPlugin.cpp:2058-2059`
- **Description**: The filename sweep did not find committed `.env`, private key, PEM, or credential files. Grep hits were code references to runtime Wi-Fi/Home Assistant/relay/Visualizer credentials, test tokens, or generated package-lock tokenizers. Firmware masks Wi-Fi, Home Assistant password, and cloud relay token in `/api/settings` GET responses. Impact: no hardcoded production API key/private key finding from source.
- **Remediation**: Keep secret scans in CI/pre-commit. Consider adding a formal gitleaks/trufflehog configuration if the repo starts accepting more integrations.

## Prioritized follow-up issue candidates

1. **High**: Add local HTTP/WebSocket authentication and narrow CORS for sensitive firmware endpoints. Candidate labels: `security`, `firmware`, `web-ui`.
2. **Medium**: Redesign cloud relay credential transport/storage away from query-string bearer tokens; add relay-origin confirmation and token rotation. Candidate labels: `security`, `web-ui`, `infrastructure`.
3. **Medium**: Stop storing Visualizer.coffee passwords in localStorage; update UX copy and add a forget-credentials control. Candidate labels: `security`, `web-ui`, `ux`.
4. **Medium**: Add a coherent Settings transaction snapshot/lock for all scalar fields and `dirty`. Candidate labels: `firmware`, `reliability`.
5. **Medium**: Add strict validation for `/api/settings` POST values and WebSocket command payloads, with JSON error responses and contract tests. Candidate labels: `firmware`, `web-ui`, `reliability`.
6. **Low/Medium UX**: Improve BLE scale connect error reporting so missing/unknown UUIDs and failed connection attempts are visible to users. Candidate labels: `firmware`, `web-ui`, `ux`, `ble`.
7. **Low UX/accessibility**: Add focus trapping and modal semantics to the mobile navigation drawer. Candidate labels: `web-ui`, `ux`, `accessibility`.
8. **Low performance**: Throttle analyzer sticky-bar layout updates and avoid redundant scroll-driven state updates. Candidate labels: `web-ui`, `performance`.

## Hardware/manual verification needed

- **Local auth threat model**: Validate on real device whether AP captive-portal setup, local Wi-Fi UI, mDNS, relay, and OTA flows need separate auth exceptions or setup-mode bypasses.
- **CORS/CSRF**: Manually verify from another LAN origin/browser whether unauthenticated POSTs and WebSocket connections can control the device in normal network mode.
- **Settings concurrency**: Stress settings saves from two browsers while the 5-second settings task is active; power-cycle and verify NVS coherence.
- **BLE scale UX**: Test missing/invalid UUID, out-of-range device, failed pairing, and disconnect/reconnect behavior with real supported scales.
- **Relay token migration**: Test migration path for existing users with relay token in localStorage/NVS before changing token transport.
- **Accessibility**: Use keyboard-only navigation and a screen reader on the web UI drawer, analyzer, settings, and history pages.
- **Performance**: Profile DashboardMerged and Shot Analyzer on a low-power phone/tablet with large shot history and active WebSocket updates.
