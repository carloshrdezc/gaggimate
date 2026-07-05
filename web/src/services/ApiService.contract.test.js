// PRO-16 — WebSocket event-handling contract audit (pinning test).
//
// PRO-16 asked us to audit the WebSocket message surface for missing/incorrect
// event handling and fix any real gaps. The audit (cross-checked in all four
// directions against docs/websocket-api.yaml, WebUIPlugin.cpp,
// ShotHistoryPlugin.cpp and web/src) found NO functional defect:
//   (a) every client `.on('evt:*' | 'res:*')` listener maps to a frame the
//       firmware actually emits — no dead listeners;
//   (b) every `evt:*` the firmware broadcasts is handled by the client
//       (evt:status/evt:relay-status special-cased in _onMessage, the rest via
//       listeners) — no dropped events;
//   (c) every `req:*` the client sends has a server dispatch branch — no
//       missing handlers;
//   (d) the only server `req:*` handlers with no client caller
//       (req:beans:load, req:history:list, req:history:get) are intentional
//       legacy/HTTP-superseded orphans (req:history:get even returns
//       "use HTTP /api/history?id=<id>").
//
// Since there was nothing to fix, this test locks the audited *client-side*
// dispatch/correlation contract so a future regression is caught mechanically.
// It pins the exact behaviour of ApiService._onMessage and request():
//   - request() correlates the reply by the client-generated `rid`;
//   - a truthy in-band `error` on the reply rejects the promise;
//   - non-status frames fan out to listeners keyed by exact `tp`;
//   - `evt:status` is routed to _onStatus and also fans out to any registered
//     evt:status listener (the fan-out falls through);
//   - `evt:relay-status` is handled inline and does NOT reach `tp` listeners.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';

// Minimal fake WebSocket — keeps everything off the network. send() is a spy so
// we can read back the frame request() actually put on the wire (incl. `rid`).
class FakeWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = FakeWebSocket.OPEN; // open immediately so request() sends
    this.listeners = {};
    this.sent = [];
    FakeWebSocket.instances.push(this);
  }
  addEventListener(type, cb) {
    (this.listeners[type] ||= []).push(cb);
  }
  removeEventListener(type, cb) {
    this.listeners[type] = (this.listeners[type] || []).filter(l => l !== cb);
  }
  close() {
    this.readyState = FakeWebSocket.CLOSED;
  }
  send(data) {
    this.sent.push(JSON.parse(data));
  }
}

let ApiService;

beforeEach(async () => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
  FakeWebSocket.instances = [];
  globalThis.WebSocket = FakeWebSocket;

  vi.resetModules();
  const mod = await import('../services/ApiService.js');
  ApiService = mod.default;
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('ApiService WebSocket contract (PRO-16 audit)', () => {
  test('request() sends the frame with a rid and resolves on the matching rid reply', async () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    const promise = api.request({ tp: 'req:profiles:list' });

    // The frame put on the wire carries the caller's tp plus a generated rid.
    expect(socket.sent).toHaveLength(1);
    const sent = socket.sent[0];
    expect(sent.tp).toBe('req:profiles:list');
    expect(typeof sent.rid).toBe('string');
    expect(sent.rid.length).toBeGreaterThan(0);

    // Firmware replies with res:profiles:list echoing the same rid.
    api._onMessage({
      data: JSON.stringify({ tp: 'res:profiles:list', rid: sent.rid, profiles: [{ id: 'a' }] }),
    });

    await expect(promise).resolves.toMatchObject({
      tp: 'res:profiles:list',
      profiles: [{ id: 'a' }],
    });
  });

  test('a truthy in-band error on the reply rejects the request', async () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    const promise = api.request({ tp: 'req:profiles:delete', id: 'missing' });
    const rid = socket.sent[0].rid;

    api._onMessage({
      data: JSON.stringify({ tp: 'res:profiles:delete', rid, error: 'Invalid profile id' }),
    });

    await expect(promise).rejects.toThrow('Invalid profile id');
  });

  test('request() correlates purely by rid (documented behaviour) — the reply tp is not re-checked', async () => {
    // AUDIT NOTE (PRO-16): request() stores returnType = `res:${tp.substring(4)}`
    // but _onMessage resolves the pending entry by `rid` alone and never asserts
    // message.tp === returnType. This is benign in practice (the firmware always
    // sets the correct res:* tp and rids are UUIDs), but the behaviour is pinned
    // here so any future change to rid-correlation is a deliberate, reviewed one.
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    const promise = api.request({ tp: 'req:flush:start' });
    const rid = socket.sent[0].rid;

    // Even a reply whose tp does not match the expected res:flush:start resolves,
    // because correlation is by rid. Do not "fix" this without a contract change.
    api._onMessage({ data: JSON.stringify({ tp: 'res:something-else', rid, success: true }) });

    await expect(promise).resolves.toMatchObject({ success: true });
  });

  test('non-status frames fan out to listeners keyed by exact tp', () => {
    const api = new ApiService();

    const otaProgress = vi.fn();
    const autotune = vi.fn();
    api.on('evt:ota-progress', otaProgress);
    api.on('evt:autotune-result', autotune);

    api._onMessage({ data: JSON.stringify({ tp: 'evt:ota-progress', phase: 1, progress: 42 }) });

    expect(otaProgress).toHaveBeenCalledTimes(1);
    expect(otaProgress).toHaveBeenCalledWith(expect.objectContaining({ progress: 42 }));
    // A different tp must not trigger an unrelated listener.
    expect(autotune).not.toHaveBeenCalled();
  });

  test('evt:status is routed to _onStatus AND still fans out to any evt:status listener', () => {
    // AUDIT NOTE (PRO-16): _onMessage special-cases evt:status into _onStatus,
    // then FALLS THROUGH to the generic listener fan-out (only evt:relay-status
    // returns early). So a screen may _onStatus-consume the mapped machine
    // signal while also registering a raw evt:status listener; both fire. This
    // is the actual contract — pinned so the fall-through isn't removed by
    // accident.
    const api = new ApiService();

    const statusListener = vi.fn();
    api.on('evt:status', statusListener);
    const onStatusSpy = vi.spyOn(api, '_onStatus');

    api._onMessage({ data: JSON.stringify({ tp: 'evt:status', ct: 93, m: 0 }) });

    expect(onStatusSpy).toHaveBeenCalledTimes(1);
    expect(statusListener).toHaveBeenCalledTimes(1);
  });

  test('evt:relay-status is handled inline and does NOT reach tp listeners', () => {
    const api = new ApiService();

    const relayListener = vi.fn();
    api.on('evt:relay-status', relayListener);

    api._onMessage({ data: JSON.stringify({ tp: 'evt:relay-status', deviceConnected: false }) });

    // _onMessage returns early after updating the machine signal — no fan-out.
    expect(relayListener).not.toHaveBeenCalled();
  });

  test('malformed and structureless frames are discarded without throwing', () => {
    const api = new ApiService();
    const listener = vi.fn();
    api.on('evt:ota-progress', listener);

    // Not valid JSON.
    expect(() => api._onMessage({ data: '{not json' })).not.toThrow();
    // Valid JSON but no `tp`.
    expect(() => api._onMessage({ data: JSON.stringify({ foo: 'bar' }) })).not.toThrow();

    expect(listener).not.toHaveBeenCalled();
  });
});
