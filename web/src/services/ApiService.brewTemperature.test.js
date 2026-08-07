// PRO-630 — device-authoritative selected-profile brew temperature, client side.
//
// The firmware (PRO-629) owns this value and publishes it on every `evt:status`
// as `bto` (effective target) + `bte` (1 when an explicit override is persisted
// for the selected profile). These tests pin the three client-side halves of
// that contract:
//
//   1. status precedence — the mapped machine status follows `bto`/`bte`, so a
//      profile switch, a reboot, or another browser's edit converges here;
//   2. older-firmware degrade — a status frame WITHOUT `bto` maps to null (never
//      a fabricated default such as 93 °C), which is what makes the dashboard
//      control render read-only instead of lying;
//   3. command payload — `req:brew-temperature:set` goes on the wire with a
//      numeric `temperature` and a `rid`, and its `ok:false` reject still
//      carries the device's current effective temperature so the UI can resync.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';

class FakeWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = FakeWebSocket.OPEN;
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
let machine;

beforeEach(async () => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
  FakeWebSocket.instances = [];
  globalThis.WebSocket = FakeWebSocket;

  // Fresh module registry per test so the module-level `machine` signal starts
  // from its declared defaults instead of leaking state between tests.
  vi.resetModules();
  const mod = await import('./ApiService.js');
  ApiService = mod.default;
  machine = mod.machine;
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

const status = (fields = {}) => JSON.stringify({ tp: 'evt:status', ct: 92, m: 1, ...fields });

describe('selected-profile brew temperature status contract (PRO-630)', () => {
  test('pre-connection state carries no brew temperature target at all', () => {
    // Explicitly NOT a number: there is nothing authoritative to show before the
    // first status frame, and inventing one (e.g. 93) is the exact failure mode
    // the acceptance criteria call out.
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(null);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(false);
  });

  test('maps bto/bte from evt:status so every browser converges on the device value', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ bto: 94.5, bte: 1 }) });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(94.5);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(true);

    // A later broadcast (another browser's edit, a profile switch that cleared
    // the override, a reboot) simply replaces it — no local state survives.
    api._onMessage({ data: status({ bto: 91, bte: 0 }) });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(91);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(false);
  });

  test('a profile switch reseeds the target from the new profile root', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ puid: 'profile-a', bto: 96, bte: 1 }) });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(96);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(true);

    // Firmware clears the override on selection, so the next status carries the
    // NEW profile's root temperature with bte=0.
    api._onMessage({ data: status({ puid: 'profile-b', bto: 90, bte: 0 }) });
    expect(machine.value.status.selectedProfileId).toBe('profile-b');
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(90);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(false);
  });

  test('older firmware without bto degrades to null instead of a fabricated default', () => {
    const api = new ApiService();

    api._onMessage({ data: status() });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(null);
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(false);

    // Non-numeric / non-finite values are treated the same way.
    api._onMessage({ data: status({ bto: null, bte: 1 }) });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(null);
    api._onMessage({ data: status({ bto: 'hot', bte: 1 }) });
    expect(machine.value.status.brewTemperatureOverrideTarget).toBe(null);
  });

  test('bte is only true for the explicit 1/true provenance flag', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ bto: 93, bte: 0 }) });
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(false);
    api._onMessage({ data: status({ bto: 93, bte: 1 }) });
    expect(machine.value.status.brewTemperatureOverrideEnabled).toBe(true);
  });
});

describe('brew temperature command payload (PRO-630)', () => {
  test('sends req:brew-temperature:set with a numeric temperature and a rid', async () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    const promise = api.request({ tp: 'req:brew-temperature:set', temperature: 95 });

    expect(socket.sent).toHaveLength(1);
    const sent = socket.sent[0];
    expect(sent.tp).toBe('req:brew-temperature:set');
    expect(sent.temperature).toBe(95);
    expect(typeof sent.rid).toBe('string');

    api._onMessage({
      data: JSON.stringify({
        tp: 'res:brew-temperature:set',
        rid: sent.rid,
        ok: true,
        temperature: 95,
        override: true,
      }),
    });

    await expect(promise).resolves.toMatchObject({ ok: true, temperature: 95, override: true });
  });

  test('a device-authoritative reject resolves with ok:false and the current effective value', async () => {
    // The race the issue calls out: a brew starts between the click and the
    // response. The firmware refuses the write but still reports the temperature
    // in force, so the caller can resync instead of showing a false success.
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    const promise = api.request({ tp: 'req:brew-temperature:set', temperature: 99 });
    const rid = socket.sent[0].rid;

    api._onMessage({
      data: JSON.stringify({
        tp: 'res:brew-temperature:set',
        rid,
        ok: false,
        temperature: 93,
        override: false,
      }),
    });

    // Not a rejection: `ok:false` is a normal in-band answer (no `error` field),
    // so the caller inspects `ok` and keeps the device's temperature.
    const response = await promise;
    expect(response.ok).toBe(false);
    expect(response.temperature).toBe(93);
  });

  test('rejects an out-of-range temperature before it reaches the wire', () => {
    const api = new ApiService();
    const socket = FakeWebSocket.instances[0];

    expect(() => api.send({ tp: 'req:brew-temperature:set', temperature: 500 })).toThrow(
      'temperature',
    );
    expect(socket.sent).toHaveLength(0);
  });
});
