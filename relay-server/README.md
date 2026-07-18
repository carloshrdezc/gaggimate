# GaggiMate Relay Server

The relay forwards WebSocket frames between an outbound ESP32 connection and remote browsers without exposing the machine to the internet.

## Configure

1. In the local GaggiMate Settings page, enable Remote Access.
2. Enter a `wss://` relay URL and a new random token, then save.
3. Copy the Remote Access Link and open it in the browser that will control the machine.
4. Confirm the displayed relay host. `ws://` and non-default/self-hosted hosts show an explicit warning.

The browser receives the setup token in the URL fragment, which is not sent in an HTTP request. It retains the token only in `sessionStorage` for the active tab; the URL itself may persist in local storage. Existing `gaggimate_relay_token` local-storage entries are migrated into the active tab and deleted on first use.

## Rotation and revocation

Change the relay token in Settings and save. The firmware reconnects using the new token; browser tabs with the prior session token cannot authenticate after reconnecting. Close tabs or use the new Remote Access Link to remove old browser credentials immediately.

## Protocol

- `GET /` - landing page
- `GET /health` - health check
- `WS /connect?role=device` - ESP32 connection
- `WS /connect?role=browser` - browser connection

`token` is never accepted as a query parameter. Clients authenticate with these WebSocket subprotocols:

- `gaggimate-relay-v1`
- `gaggimate-token-<base64url-token>`

The server validates both subprotocols before selecting the session. It uses the validated token only as the in-memory/Durable Object session key and never logs it.

## Local development

```bash
npm ci
npm test
npm run dev
```
