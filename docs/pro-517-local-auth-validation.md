# PRO-517 local auth validation

## Model

- On first boot, firmware generates a 128-bit device-local admin token and persists it in the existing `controller` NVS namespace as `admin_token`.
- A saved-Wi-Fi installation upgrading from before local auth starts a recovery AP until its owner explicitly completes AP provisioning. This physical-presence recovery channel returns the token; normal STA responses never do.
- Normal STA/LAN requests to sensitive HTTP routes, including `/api/history/*`, require an HTTP bearer token. The sole query-token exception is `GET /api/diag/log.txt` and `GET /api/diag/log.1`, because their browser download anchors cannot attach headers; query tokens are rejected for `/api/settings`, scale/control, history, core-dump, and every other route.
- Local WebSocket clients must first send `{ "tp": "req:auth", "token": "<token>"`; every later `req:*` command is rejected until that per-client session is authenticated. Relay frames remain authenticated by the relay's existing token boundary.
- The AP fallback is the deliberate physical-presence setup exception. Only `/api/settings` is unauthenticated there, and that response returns the bootstrap token. The web UI persists it locally and presents it in later HTTP/WS requests.
- Normal builds emit no CORS headers. `GAGGIMATE_DEVELOPMENT_CORS=1` is an explicit local Vite exception restricted to `http://localhost:5173`.

## Manual on-device smoke test (post-merge)

1. Erase NVS or flash a first-boot device, join the fallback AP, and open the embedded UI. Confirm Settings loads and the UI reconnects its WebSocket after storing the returned token.
2. Before saving Wi-Fi settings, use **Copy Wi-Fi auth handoff link**. Confirm the link targets `<hostname>.local` and carries the token after `#` (not in its request path/query). Save Wi-Fi settings, join the STA network, open the copied link, and confirm it stores the token for the STA origin then clears the fragment.
3. Upgrade a device that already has Wi-Fi credentials but no local-auth provisioning marker. Confirm it exposes the recovery AP, allows only AP `/api/settings` bootstrap without a token, and keeps that AP until the handoff action explicitly completes provisioning.
4. From a second browser/device, confirm unauthenticated `GET /api/settings`, `POST /api/settings`, `/api/scales/*`, `/api/history/*`, `/api/core-dump`, and `/api/diag/log.*` return 401. Confirm `?localAuthToken=<token>` remains rejected for `/api/settings` and `/api/scales/list`, works only for the two diagnostic-log GET downloads, and that a bearer header works for each protected route.
5. In DevTools, connect to `/ws` and send `req:process:activate`, `req:ota-start`, and `req:autotune-start` before `req:auth`; confirm no controller side effect. Authenticate with the token, repeat a safe command, and confirm it succeeds.
6. Confirm normal responses do not contain `Access-Control-Allow-Origin`; rebuild only with the explicit development flag to validate the localhost exception.
