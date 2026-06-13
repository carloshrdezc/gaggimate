import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  listBeans,
  migrateLegacyBeansToDevice,
  removeBean,
  saveBean,
} from './beanManager.js';

const BEANS_STORAGE_KEY = 'gaggimate-beans';
const BEANS_PENDING_STORAGE_KEY = 'gaggimate-beans-pending';

function readKey(key) {
  const raw = localStorage.getItem(key);
  return raw ? JSON.parse(raw) : null;
}

// A fake ApiService whose request() is driven by `handler`. The socket appears
// OPEN so hasConnectedApi() returns true.
function makeApi(handler) {
  return {
    socket: { readyState: WebSocket.OPEN },
    request: vi.fn(handler),
  };
}

// A disconnected ApiService (no open socket) so the offline branch is taken.
function makeOfflineApi() {
  return { socket: { readyState: WebSocket.CLOSED }, request: vi.fn() };
}

// A device simulator mirroring the firmware's authoritative, id-keyed bean
// store: req:beans:save upserts by id (echoing the canonical bean and owning
// the id/timestamp); req:beans:list returns the full set sorted most-recent
// first. Beans are uncapped — no eviction to model (unlike grinders).
function makeDevice({ initial = [] } = {}) {
  const byId = new Map();
  for (const bean of initial) {
    byId.set(bean.id, { ...bean });
  }
  const saves = [];

  const handler = async ({ tp, bean, id }) => {
    if (tp === 'req:beans:list') {
      const beans = [...byId.values()].sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
      return { beans };
    }
    if (tp === 'req:beans:save') {
      saves.push(bean);
      // Device assigns/keeps the id and echoes the canonical record back.
      const stored = { ...bean };
      byId.set(stored.id, stored);
      return { bean: stored };
    }
    if (tp === 'req:beans:delete') {
      byId.delete(id);
      return {};
    }
    return {};
  };

  return {
    api: makeApi(handler),
    saves,
    current: () => [...byId.values()],
    ids: () => [...byId.keys()],
  };
}

function makeBean(overrides = {}) {
  return {
    id: 'bean-1',
    name: 'House Blend',
    roaster: 'Acme',
    quantity: 250,
    updatedAt: 1000,
    createdAt: 1000,
    ...overrides,
  };
}

describe('beanManager', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  describe('saveBean offline tracking (CAR-373 Bug 2 & 3)', () => {
    it('marks an offline-created bean as pending and persists it locally', async () => {
      const api = makeOfflineApi();
      const saved = await saveBean(api, makeBean({ id: 'bean-offline' }));

      expect(api.request).not.toHaveBeenCalled();
      expect(saved.id).toBe('bean-offline');
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['bean-offline']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['bean-offline']);
    });

    it('does not mark a successfully-synced bean as pending', async () => {
      const { api } = makeDevice();
      await saveBean(api, makeBean({ id: 'bean-online' }));

      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['bean-online']);
    });

    it('falls back to pending when an online save throws (failed save retried, not dropped)', async () => {
      const api = makeApi(async ({ tp }) => {
        if (tp === 'req:beans:save') throw new Error('socket drop');
        return { beans: [] };
      });
      const saved = await saveBean(api, makeBean({ id: 'bean-failed' }));

      expect(saved.id).toBe('bean-failed');
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['bean-failed']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['bean-failed']);
    });
  });

  describe('listBeans pending drain (offline-create reaches device on reconnect)', () => {
    it('pushes an offline-created bean to the device on the next connected listBeans', async () => {
      // Simulate offline creation: bean in the local mirror + pending set.
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'bean-offline' })]));
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['bean-offline']));

      const { api, saves, ids } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Device Bean' })] });

      const result = await listBeans(api);

      // The pending bean was pushed exactly once and is now on the device.
      expect(saves.map(b => b.id)).toEqual(['bean-offline']);
      expect(ids().sort()).toEqual(['bean-offline', 'device-1']);
      // Pending cleared once the device echoed it back.
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result.map(b => b.id).sort()).toEqual(['bean-offline', 'device-1']);
    });

    it('returns the local cache without touching the device when disconnected', async () => {
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'cached' })]));
      const api = makeOfflineApi();
      const result = await listBeans(api);
      expect(result.map(b => b.id)).toEqual(['cached']);
      expect(api.request).not.toHaveBeenCalled();
    });

    it('id-keyed dedup: does not re-push a bean the device already has', async () => {
      // Pending id is already present on the device (e.g. saved from another tab).
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'shared' })]));
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['shared']));
      const { api, saves } = makeDevice({ initial: [makeBean({ id: 'shared' })] });

      const result = await listBeans(api);

      // It is pushed once (to be safe) but the device dedups by id — no
      // duplicate is created, and pending is cleared because it came back.
      expect(saves.length).toBeLessThanOrEqual(1);
      expect(result.filter(b => b.id === 'shared').length).toBe(1);
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
    });
  });

  describe('migrateLegacyBeansToDevice (CAR-373 Bug 1: populated device)', () => {
    it('migrates legacy beans even when the device already has other beans (no data loss)', async () => {
      // Legacy localStorage-only beans, device already populated with a different bean.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy-1', name: 'Legacy A' }), makeBean({ id: 'legacy-2', name: 'Legacy B' })]),
      );
      const { api, ids } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Already Here' })] });

      const result = await migrateLegacyBeansToDevice(api);

      // Both legacy beans reached the device, alongside the pre-existing one.
      expect(ids().sort()).toEqual(['device-1', 'legacy-1', 'legacy-2']);
      expect(result.map(b => b.id).sort()).toEqual(['device-1', 'legacy-1', 'legacy-2']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
    });

    it('is idempotent across repeated calls (no duplicate re-push)', async () => {
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy-1', name: 'Legacy A' })]),
      );
      const { api, saves, ids } = makeDevice({ initial: [makeBean({ id: 'device-1' })] });

      await migrateLegacyBeansToDevice(api);
      const savesAfterFirst = saves.length;
      await migrateLegacyBeansToDevice(api);
      await migrateLegacyBeansToDevice(api);

      // No additional pushes after the first migration; device set unchanged.
      expect(saves.length).toBe(savesAfterFirst);
      expect(ids().sort()).toEqual(['device-1', 'legacy-1']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
    });

    it('returns the device list unchanged when there are no legacy beans', async () => {
      const { api, saves } = makeDevice({ initial: [makeBean({ id: 'device-1' })] });
      const result = await migrateLegacyBeansToDevice(api);
      expect(saves.length).toBe(0);
      expect(result.map(b => b.id)).toEqual(['device-1']);
    });
  });

  describe('listBeans retains failed migrations (retry)', () => {
    it('keeps an id pending and surfaces the bean when the device save fails', async () => {
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'offline-1' })]));
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['offline-1']));

      const api = makeApi(async ({ tp }) => {
        if (tp === 'req:beans:list') return { beans: [makeBean({ id: 'device-1' })] };
        if (tp === 'req:beans:save') throw new Error('device write error');
        return {};
      });

      const result = await listBeans(api);

      // The unsynced bean survives in the pending set and is merged for the UI.
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['offline-1']);
      expect(result.map(b => b.id).sort()).toEqual(['device-1', 'offline-1']);
    });

    it('eventually syncs a previously-failed bean on a later connected call', async () => {
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'offline-1' })]));
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['offline-1']));

      let failNext = true;
      const byId = new Map([['device-1', makeBean({ id: 'device-1' })]]);
      const saves = [];
      const api = makeApi(async ({ tp, bean }) => {
        if (tp === 'req:beans:list') {
          return { beans: [...byId.values()] };
        }
        if (tp === 'req:beans:save') {
          if (failNext) {
            failNext = false;
            throw new Error('transient');
          }
          saves.push(bean);
          byId.set(bean.id, { ...bean });
          return { bean: { ...bean } };
        }
        return {};
      });

      await listBeans(api); // save fails, id stays pending
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['offline-1']);

      const result = await listBeans(api); // retry succeeds
      expect(saves.map(b => b.id)).toEqual(['offline-1']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result.map(b => b.id).sort()).toEqual(['device-1', 'offline-1']);
    });
  });

  describe('removeBean clears pending', () => {
    it('does not resurrect a bean removed while offline', async () => {
      localStorage.setItem(BEANS_STORAGE_KEY, JSON.stringify([makeBean({ id: 'offline-1' })]));
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['offline-1']));

      const offline = makeOfflineApi();
      await removeBean(offline, 'offline-1');

      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);

      // On a later connected drain the device must not receive the removed bean.
      const { api, saves } = makeDevice();
      await listBeans(api);
      expect(saves.length).toBe(0);
    });
  });
});
