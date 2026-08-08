// PRO-640 — build-time BLE-scale capability (`bse`), client side.
//
// A GAGGIMATE_ENABLE_BLE_SCALE=0 firmware hardcodes `bc: false` in every
// `evt:status` while volumetric brewing still reaches its target through flow
// estimation, so "scale disconnected" there is a permanent, expected state
// rather than a fault. `bse` is the signal that lets the dashboard tell the two
// apart. These tests pin the wire contract:
//
//   1. a flags-off device (`bse: false`) maps to bluetoothScaleEnabled false;
//   2. a BLE-capable device (`bse: true`) maps to true and keeps `bc` intact;
//   3. firmware OLDER than this field (no `bse` at all) defaults to TRUE — such
//      builds always have BLE compiled in, and defaulting false would silence
//      the genuine "this volumetric brew needs the scale" warning everywhere.

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

describe('BLE-scale feature flag status contract (PRO-640)', () => {
  test('pre-connection state assumes the BLE-scale feature is present', () => {
    expect(machine.value.status.bluetoothScaleEnabled).toBe(true);
  });

  test('a BLE-scale-disabled build reports bse:false alongside its hardcoded bc:false', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ bse: false, bc: false, cw: 0, bt: 1 }) });
    expect(machine.value.status.bluetoothScaleEnabled).toBe(false);
    // The runtime connection field is unchanged — `bse` only adds the reason.
    expect(machine.value.status.bluetoothConnected).toBe(false);
    // ...and a volumetric target is still reported, because flow estimation
    // carries the shot on that build.
    expect(machine.value.status.brewTarget).toBe(true);
  });

  test('a BLE-capable build reports bse:true and keeps the connection state separate', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ bse: true, bc: false }) });
    expect(machine.value.status.bluetoothScaleEnabled).toBe(true);
    expect(machine.value.status.bluetoothConnected).toBe(false);

    api._onMessage({ data: status({ bse: true, bc: true, cw: 18.2 }) });
    expect(machine.value.status.bluetoothScaleEnabled).toBe(true);
    expect(machine.value.status.bluetoothConnected).toBe(true);
    expect(machine.value.status.currentWeight).toBe(18.2);
  });

  test('firmware without bse defaults to enabled so the scale warning is not silenced', () => {
    const api = new ApiService();

    api._onMessage({ data: status({ bc: false }) });
    expect(machine.value.status.bluetoothScaleEnabled).toBe(true);
  });
});
