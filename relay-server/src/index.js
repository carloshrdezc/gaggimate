'use strict';

export { RelaySession } from './relay.js';

const HTML = `<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><title>GaggiMate Relay</title>
<style>body{font-family:sans-serif;max-width:480px;margin:80px auto;padding:0 16px}
input{width:100%;padding:8px;margin:8px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px}
button{padding:10px 24px;background:#1a1a1a;color:#fff;border:none;border-radius:4px;cursor:pointer}.note{color:#666;font-size:0.85em}</style>
</head>
<body>
<h1>GaggiMate Relay</h1>
<p>Enter your token to open your machine's Web UI:</p>
<input type="password" id="token" placeholder="Relay token" />
<br/>
<input type="url" id="uiUrl" placeholder="Web UI URL (e.g. http://gaggimate.local)" />
<br/>
<button onclick="connect()">Connect</button>
<p class="note">The token is placed in the URL fragment, which browsers do not send to the web server. You will confirm the relay host before connecting.</p>
<script>
function connect() {
  const token = document.getElementById('token').value.trim();
  const uiUrl = document.getElementById('uiUrl').value.trim();
  if (!token) { alert('Enter a token'); return; }
  if (!uiUrl) { alert('Enter the Web UI URL'); return; }
  const wsBase = window.location.origin.replace(/^http/, 'ws');
  let dest;
  try { dest = new URL(uiUrl); } catch { alert('Enter a valid Web UI URL'); return; }
  dest.hash = new URLSearchParams({ relay: wsBase, token }).toString();
  window.location.href = dest.toString();
}
</script>
</body></html>`;

export function relayTokenFromProtocols(protocols) {
  const tokenProtocol = (protocols || '').split(',').map(value => value.trim()).find(value => value.startsWith('gaggimate-token-'));
  if (!tokenProtocol || !(protocols || '').split(',').map(value => value.trim()).includes('gaggimate-relay-v1')) return null;
  const encoded = tokenProtocol.slice('gaggimate-token-'.length);
  if (!/^[A-Za-z0-9_-]+$/.test(encoded)) return null;
  try {
    const base64 = encoded.replace(/-/g, '+').replace(/_/g, '/');
    const bytes = Uint8Array.from(atob(base64 + '='.repeat((4 - base64.length % 4) % 4)), byte => byte.charCodeAt(0));
    const token = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    return token ? token : null;
  } catch {
    return null;
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === '/health') return new Response('ok', { headers: { 'Content-Type': 'text/plain' } });

    if (url.pathname === '/connect') {
      const role = url.searchParams.get('role');
      if (url.searchParams.has('token') || (role !== 'device' && role !== 'browser')) {
        return new Response('Token query parameters are not accepted and role must be device or browser', { status: 400 });
      }
      const token = relayTokenFromProtocols(request.headers.get('Sec-WebSocket-Protocol'));
      if (!token) return new Response('Missing relay authentication subprotocol', { status: 400 });

      const id = env.RELAY.idFromName(token);
      return env.RELAY.get(id).fetch(request);
    }

    if (url.pathname === '/') return new Response(HTML, { headers: { 'Content-Type': 'text/html; charset=utf-8' } });
    return new Response('Not Found', { status: 404 });
  },
};
