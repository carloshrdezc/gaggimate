# PRO-517 local auth validation

## Model

- On first boot, firmware generates a 128-bit device-local admin token and persists it in the existing `controller` NVS namespace as `admin_token`.
- Normal STA/LAN requests to sensitive HTTP routes require `Authorization: Bearer <token>` (a `localAuthToken` query parameter exists only for browser download links that cannot set request headers).
- Local WebSocket clients must first send `{ "tp": "req:auth", "token": "<token>"`; every later `req:*` command is rejected until that per-client session is authenticated. Relay frames remain authenticated by the relay's existing token boundary.
- The AP fallback is the deliberate physical-presence setup exception. Only `/api/settings` is unauthenticated there, and that response returns the bootstrap token. The web UI persists it locally and presents it in later HTTP/WS requests.
- Normal builds emit no CORS headers. `GAGGIMATE_DEVELOPMENT_CORS=1` is an explicit local Vite exception restricted to `http://localhost:5173`.

## Manual on-device smoke test (post-merge)

1. Erase NVS or flash a first-boot device, join the fallback AP, and open the embedded UI. Confirm Settings loads and the UI reconnects its WebSocket after storing the returned token.
2. Join the configured STA network. From a second browser/device, confirm `GET /api/settings`, `POST /api/settings`, `/api/scales/*`, `/api/core-dump`, and `/api/diag/log.*` return 401 without a bearer token.
3. In DevTools, connect to `/ws` and send `req:process:activate`, `req:ota-start`, and `req:autotune-start` before `req:auth`; confirm no controller side effect. Authenticate with the token, repeat a safe command, and confirm it succeeds.
4. Confirm normal responses do not contain `Access-Control-Allow-Origin`; rebuild only with the explicit development flag to validate the localhost exception.
