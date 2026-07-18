'use strict';

const { WebSocketServer, WebSocket } = require('ws');
const http = require('http');

const PORT = process.env.PORT || 8080;
const PREFIX = 'gaggimate-token-';

function relayTokenFromProtocols(protocols = '') {
  const values = protocols.split(',').map(value => value.trim());
  const encoded = values.find(value => value.startsWith(PREFIX))?.slice(PREFIX.length);
  if (!values.includes('gaggimate-relay-v1') || !encoded || !/^[A-Za-z0-9_-]+$/.test(encoded)) return null;
  try {
    const base64 = encoded.replace(/-/g, '+').replace(/_/g, '/');
    const bytes = Buffer.from(base64 + '='.repeat((4 - base64.length % 4) % 4), 'base64');
    const token = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    return token ? token : null;
  } catch {
    return null;
  }
}

const server = http.createServer((req, res) => {
  if (req.url === '/health') return res.end('ok');
  if (req.url === '/') return res.end('GaggiMate Relay: use the web UI remote-access link to connect.');
  res.writeHead(404).end();
});
const wss = new WebSocketServer({ noServer: true });
const sessions = new Map();

function getSession(token) {
  if (!sessions.has(token)) sessions.set(token, { device: null, browsers: new Set() });
  return sessions.get(token);
}
function cleanupSession(token) {
  const session = sessions.get(token);
  if (session && !session.device && session.browsers.size === 0) sessions.delete(token);
}

server.on('upgrade', (request, socket, head) => {
  const url = new URL(request.url, 'http://localhost');
  const role = url.searchParams.get('role');
  const token = !url.searchParams.has('token') && (role === 'device' || role === 'browser')
    ? relayTokenFromProtocols(request.headers['sec-websocket-protocol'])
    : null;
  if (url.pathname !== '/connect' || !token) return socket.destroy();
  wss.handleUpgrade(request, socket, head, ws => wss.emit('connection', ws, token, role));
});

wss.on('connection', (ws, token, role) => {
  const session = getSession(token);
  if (role === 'device') {
    if (session.device?.readyState === WebSocket.OPEN) session.device.close(1000, 'Replaced by new device connection');
    session.device = ws;
    for (const browser of session.browsers) if (browser.readyState === WebSocket.OPEN) browser.send(JSON.stringify({ tp: 'evt:relay-status', deviceConnected: true }));
    ws.on('message', data => { for (const browser of session.browsers) if (browser.readyState === WebSocket.OPEN) browser.send(data); });
    ws.on('close', () => {
      if (session.device === ws) session.device = null;
      for (const browser of session.browsers) if (browser.readyState === WebSocket.OPEN) browser.send(JSON.stringify({ tp: 'evt:relay-status', deviceConnected: false }));
      cleanupSession(token);
    });
  } else {
    session.browsers.add(ws);
    ws.send(JSON.stringify({ tp: 'evt:relay-status', deviceConnected: session.device?.readyState === WebSocket.OPEN }));
    ws.on('message', data => { if (session.device?.readyState === WebSocket.OPEN) session.device.send(data); });
    ws.on('close', () => { session.browsers.delete(ws); cleanupSession(token); });
  }
});

server.listen(PORT, () => console.log(`GaggiMate relay server listening on port ${PORT}`));
