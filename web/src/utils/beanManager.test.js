import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  inferBeanForShot,
  inferBeanIdForShot,
  isBeanRecordedForShot,
  listBeans,
  migrateLegacyBeansToDevice,
  removeBean,
  saveBean,
} from './beanManager.js';

const BEANS_STORAGE_KEY = 'gaggimate-beans';
const BEANS_PENDING_STORAGE_KEY = 'gaggimate-beans-pending';
const BEANS_LEGACY_MIGRATED_KEY = 'gaggimate-beans-legacy-migrated';
const LEGACY_BEAN_MIGRATION_KEY = 'gaggimate-beans-migrated';

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

  describe('drainPendingBeans reconciles device-regenerated ids (CAR-373 P1)', () => {
    // A device simulator that mirrors the firmware's parseBean + saveBean
    // behavior for UNSAFE legacy/imported ids: req:beans:save clears the unsafe
    // id and regenerates a new SAFE id via generateShortID(), echoes the bean
    // back under the regenerated id, and req:beans:list returns the regenerated
    // bean — the original (unsafe) id never appears in the list.
    function makeRegeneratingDevice({ initial = [], isUnsafe, regenerate } = {}) {
      const byId = new Map();
      for (const bean of initial) byId.set(bean.id, { ...bean });
      const saves = [];
      let counter = 0;
      const handler = async ({ tp, bean, id }) => {
        if (tp === 'req:beans:list') {
          const beans = [...byId.values()].sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
          return { beans };
        }
        if (tp === 'req:beans:save') {
          saves.push(bean);
          const safeId = isUnsafe(bean.id)
            ? (regenerate ? regenerate(bean, counter++) : `safe-${counter++}`)
            : bean.id;
          const stored = { ...bean, id: safeId };
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

    it('clears the original pending id, stores the regenerated bean (no stale dup), and is idempotent', async () => {
      // Offline-created bean with an UNSAFE legacy/imported id.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy/unsafe id', name: 'Imported Bean' })]),
      );
      localStorage.setItem(BEANS_PENDING_STORAGE_KEY, JSON.stringify(['legacy/unsafe id']));

      const { api, saves, ids } = makeRegeneratingDevice({
        isUnsafe: id => /[^A-Za-z0-9-]/.test(id), // unsafe if it has chars firmware would strip
        regenerate: () => 'regen-abc123',
      });

      // --- First drain ---
      const result = await listBeans(api);

      // The bean was pushed once; the device regenerated a safe id.
      expect(saves.map(b => b.id)).toEqual(['legacy/unsafe id']);
      expect(ids()).toEqual(['regen-abc123']);

      // Pending set is EMPTY — the original (unsafe) id was cleared even though
      // it never appeared in req:beans:list (it was confirmed via the echo).
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);

      // Local mirror holds ONLY the regenerated bean — the stale original-id
      // payload is gone (not shadowing or duplicating the device record).
      const mirrorIds = readKey(BEANS_STORAGE_KEY).map(b => b.id);
      expect(mirrorIds).toEqual(['regen-abc123']);
      expect(mirrorIds).not.toContain('legacy/unsafe id');
      expect(result.map(b => b.id)).toEqual(['regen-abc123']);

      // --- Second drain (idempotency): must push NOTHING new ---
      const savesAfterFirst = saves.length;
      const result2 = await listBeans(api);

      expect(saves.length).toBe(savesAfterFirst); // no additional req:beans:save
      expect(ids()).toEqual(['regen-abc123']); // no duplicate created on device
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result2.map(b => b.id)).toEqual(['regen-abc123']);
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

  describe('CAR-373 P1: read path does not self-trigger the change event', () => {
    it('connected listBeans with empty pending + populated device dispatches ZERO beans-library-changed events and issues exactly ONE req:beans:list', async () => {
      // Simulate a normal connected page load / popover read: device has beans,
      // the local cache mirrors them, and there is NOTHING pending.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'device-1', name: 'Cached Mirror' })]),
      );
      // No pending set at all (pure read).
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();

      const { api } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Cached Mirror' })] });

      // Spy on the public change event. A listener on this event (ShotHistory /
      // Beans pages) re-calls listBeans(); if even one event fires here the loop
      // would re-enter. We assert ZERO dispatches of beans-library-changed.
      const dispatchSpy = vi.spyOn(window, 'dispatchEvent');

      const result = await listBeans(api);

      const beansChangedDispatches = dispatchSpy.mock.calls.filter(
        ([event]) => event && event.type === 'beans-library-changed',
      );
      expect(beansChangedDispatches.length).toBe(0);

      // Exactly ONE req:beans:list — no re-entrant second read, no save.
      const listCalls = api.request.mock.calls.filter(([msg]) => msg?.tp === 'req:beans:list');
      const saveCalls = api.request.mock.calls.filter(([msg]) => msg?.tp === 'req:beans:save');
      expect(listCalls.length).toBe(1);
      expect(saveCalls.length).toBe(0);

      // Returned list is still the authoritative device list.
      expect(result.map(b => b.id)).toEqual(['device-1']);

      dispatchSpy.mockRestore();
    });
  });

  describe('CAR-373 P2: stale cache does not resurrect a bean deleted elsewhere', () => {
    it('after the legacy-migrated flag is set, a cache-only bean absent from the device is NOT re-pushed nor returned', async () => {
      // Durable flag already set: the one-time legacy rescue has run before.
      localStorage.setItem(BEANS_LEGACY_MIGRATED_KEY, JSON.stringify(true));
      // Local cache still holds a copy of a bean that was DELETED on another
      // client/display; it is absent from the device's authoritative list.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([
          makeBean({ id: 'deleted-elsewhere', name: 'Stale Copy' }),
          makeBean({ id: 'device-1', name: 'Live Bean' }),
        ]),
      );
      // No explicit pending writes — the deletion is genuine, not an unsynced write.
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();

      const { api, saves } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Live Bean' })] });

      const result = await migrateLegacyBeansToDevice(api);

      // The deleted bean must NOT be seeded into pending...
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).not.toContain('deleted-elsewhere');
      // ...nor re-pushed to the device.
      expect(saves.map(b => b.id)).not.toContain('deleted-elsewhere');
      // The returned + cached list matches the device (deleted bean is gone).
      expect(result.map(b => b.id)).toEqual(['device-1']);
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['device-1']);
    });
  });

  describe('CAR-373 P2: legacy beans still migrate once (guards against re-introducing Bug 1)', () => {
    it('on the FIRST connected load (flag unset) with a populated device, a legacy cache-only bean IS seeded + pushed', async () => {
      // Fresh browser: no legacy-migrated flag yet.
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBeNull();
      // A genuine legacy/offline bean that predates the pending-set mechanism
      // (in the cache, not in the explicit pending set), on a POPULATED device.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy-only', name: 'Predates Pending' })]),
      );
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();

      const { api, saves, ids } = makeDevice({
        initial: [makeBean({ id: 'device-1', name: 'Already Here' })],
      });

      const result = await migrateLegacyBeansToDevice(api);

      // The legacy bean was rescued: seeded, pushed, and now on the device
      // alongside the pre-existing one (no Bug-1 strand on a populated device).
      expect(saves.map(b => b.id)).toContain('legacy-only');
      expect(ids().sort()).toEqual(['device-1', 'legacy-only']);
      expect(result.map(b => b.id).sort()).toEqual(['device-1', 'legacy-only']);
      // Pending cleared (confirmed) and the durable flag is now set.
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBe(true);
    });
  });

  describe('CAR-373 P2 upgrade path: pre-existing old migration flag is honored (no resurrect)', () => {
    it('with gaggimate-beans-migrated=true and the new key UNSET, a cache-only bean deleted elsewhere is NOT seeded nor re-saved, and the deletion stays gone', async () => {
      // Upgrade scenario: this browser ran the OLD migration (old flag true)
      // and still holds a `gaggimate-beans` cache that mirrored the device list
      // at that time. The NEW durable key has never been set on this browser.
      localStorage.setItem(LEGACY_BEAN_MIGRATION_KEY, JSON.stringify(true));
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBeNull();
      // The cache still holds a bean that was DELETED on another client — it is
      // absent from the device's authoritative list now.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([
          makeBean({ id: 'deleted-elsewhere', name: 'Stale Upgrade Copy' }),
          makeBean({ id: 'device-1', name: 'Live Bean' }),
        ]),
      );
      // No explicit pending writes — the deletion is genuine, not an unsynced write.
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();

      const { api, saves } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Live Bean' })] });

      const result = await migrateLegacyBeansToDevice(api);

      // The deleted bean must NOT be seeded into pending...
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).not.toContain('deleted-elsewhere');
      // ...nor re-saved to the device (req:beans:save not called for it).
      expect(saves.map(b => b.id)).not.toContain('deleted-elsewhere');
      // Returned + cached list matches the device (deleted bean stays gone).
      expect(result.map(b => b.id)).toEqual(['device-1']);
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['device-1']);
      // The new durable flag is now set (the old flag was promoted to it).
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBe(true);
    });
  });

  describe('CAR-373 P2 fresh browser still rescues (neither flag set)', () => {
    it('with NEITHER migration flag set, a genuine legacy cache-only bean on a populated device IS seeded + pushed', async () => {
      // Genuinely-fresh browser: neither the old nor the new flag is set.
      expect(readKey(LEGACY_BEAN_MIGRATION_KEY)).toBeNull();
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBeNull();
      // A genuine legacy/offline bean that predates the pending-set mechanism.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy-only', name: 'Predates Pending' })]),
      );
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();

      const { api, saves, ids } = makeDevice({
        initial: [makeBean({ id: 'device-1', name: 'Already Here' })],
      });

      const result = await migrateLegacyBeansToDevice(api);

      // The legacy bean was rescued: seeded, pushed, and now on the device.
      expect(saves.map(b => b.id)).toContain('legacy-only');
      expect(ids().sort()).toEqual(['device-1', 'legacy-only']);
      expect(result.map(b => b.id).sort()).toEqual(['device-1', 'legacy-only']);
      // Pending cleared (confirmed) and the durable flag is now set.
      expect(readKey(BEANS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBe(true);
    });
  });

  // Counts `beans-library-changed` events fired during `fn()`. Uses a real
  // addEventListener so it observes exactly what app listeners (ShotHistory /
  // Beans pages) would receive.
  async function countBeansChanged(fn) {
    let count = 0;
    const listener = () => {
      count += 1;
    };
    window.addEventListener('beans-library-changed', listener);
    try {
      await fn();
    } finally {
      window.removeEventListener('beans-library-changed', listener);
    }
    return count;
  }

  describe('CAR-373 M1: migration dispatches beans-library-changed exactly once', () => {
    it('a connected migration that seeds + confirms >=1 pending bean dispatches EXACTLY ONCE (was twice)', async () => {
      // Legacy cache-only bean on a populated device, fresh browser (no flag),
      // so seedPendingFromLegacy seeds it and the drain confirms the sync.
      expect(readKey(BEANS_LEGACY_MIGRATED_KEY)).toBeNull();
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'legacy-only', name: 'Predates Pending' })]),
      );
      const { api, ids } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Already Here' })] });

      const count = await countBeansChanged(() => migrateLegacyBeansToDevice(api));

      // The bean reached the device (sync confirmed), and the event fired ONCE.
      expect(ids().sort()).toEqual(['device-1', 'legacy-only']);
      expect(count).toBe(1);
    });
  });

  describe('CAR-373 M1 non-regression: no-op connected read stays silent (P1)', () => {
    it('a connected listBeans() with empty pending + a device list dispatches ZERO events', async () => {
      // Pure read: device has beans, cache mirrors them, nothing pending. The
      // M1 change must NOT have made reads noisy.
      localStorage.setItem(
        BEANS_STORAGE_KEY,
        JSON.stringify([makeBean({ id: 'device-1', name: 'Cached Mirror' })]),
      );
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toBeNull();
      const { api } = makeDevice({ initial: [makeBean({ id: 'device-1', name: 'Cached Mirror' })] });

      const count = await countBeansChanged(() => listBeans(api));

      expect(count).toBe(0);
    });
  });

  describe('CAR-373 M2: offline saveBean honors suppressEvent', () => {
    it('offline saveBean with suppressEvent persists the bean (stored + pending) but dispatches ZERO events', async () => {
      const api = makeOfflineApi();

      const count = await countBeansChanged(() =>
        saveBean(api, makeBean({ id: 'bean-offline' }), { suppressEvent: true }),
      );

      // No device call, no event...
      expect(api.request).not.toHaveBeenCalled();
      expect(count).toBe(0);
      // ...but the bean was persisted locally and marked pending.
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['bean-offline']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['bean-offline']);
    });

    it('offline saveBean WITHOUT suppressEvent DOES dispatch once (proves the option is honored, not always-silent)', async () => {
      const api = makeOfflineApi();

      const count = await countBeansChanged(() =>
        saveBean(api, makeBean({ id: 'bean-offline-2' })),
      );

      expect(count).toBe(1);
      expect(readKey(BEANS_STORAGE_KEY).map(b => b.id)).toEqual(['bean-offline-2']);
      expect(readKey(BEANS_PENDING_STORAGE_KEY)).toEqual(['bean-offline-2']);
    });
  });

  // PRO-422: device-recorded bean (beanId/beanType on the shot record or its
  // notes) is authoritative and must win over the per-browser localStorage
  // selection-event log, which is now only a legacy fallback.
  describe('inferBeanForShot / inferBeanIdForShot precedence (PRO-422)', () => {
    const BEAN_SELECTION_EVENTS_KEY = 'gaggimate-bean-selection-events';

    function seedSelectionEvent({ profileLabel, beanId, beanName, selectedAtMs }) {
      localStorage.setItem(
        BEAN_SELECTION_EVENTS_KEY,
        JSON.stringify([{ profileLabel, beanId, beanName, selectedAtMs }]),
      );
    }

    it('prefers a device-recorded beanId in notes over the localStorage guess', () => {
      seedSelectionEvent({
        profileLabel: 'Espresso',
        beanId: 'guessed-bean',
        beanName: 'Guessed Bean',
        selectedAtMs: 1000,
      });
      const shot = {
        profile: 'Espresso',
        timestamp: 5, // 5000ms > 1000ms, so the guess would otherwise match
        notes: { beanId: 'recorded-bean', beanType: 'Recorded Bean' },
      };
      expect(inferBeanIdForShot(shot)).toBe('recorded-bean');
      expect(inferBeanForShot(shot)).toBe('Recorded Bean');
    });

    it('prefers a device-recorded beanType (name) over the localStorage guess', () => {
      seedSelectionEvent({
        profileLabel: 'Espresso',
        beanId: 'guessed-bean',
        beanName: 'Guessed Bean',
        selectedAtMs: 1000,
      });
      const shot = { profile: 'Espresso', timestamp: 5, notes: { beanType: 'Recorded Bean' } };
      expect(inferBeanForShot(shot)).toBe('Recorded Bean');
    });

    it('falls back to the localStorage selection-event log only when nothing was recorded', () => {
      seedSelectionEvent({
        profileLabel: 'Espresso',
        beanId: 'legacy-bean',
        beanName: 'Legacy Bean',
        selectedAtMs: 1000,
      });
      const shot = { profile: 'Espresso', timestamp: 5, notes: {} };
      expect(inferBeanIdForShot(shot)).toBe('legacy-bean');
      expect(inferBeanForShot(shot)).toBe('Legacy Bean');
    });

    it('returns empty when there is neither a recorded bean nor a matching selection event', () => {
      const shot = { profile: 'Espresso', timestamp: 5, notes: {} };
      expect(inferBeanIdForShot(shot)).toBe('');
      expect(inferBeanForShot(shot)).toBe('');
    });
  });

  describe('isBeanRecordedForShot (PRO-422)', () => {
    it('is true when the shot notes carry a device-recorded beanId', () => {
      expect(isBeanRecordedForShot({ notes: { beanId: 'bean-1' } })).toBe(true);
    });

    it('is true when the shot notes carry a device-recorded beanType', () => {
      expect(isBeanRecordedForShot({ notes: { beanType: 'Some Bean' } })).toBe(true);
    });

    it('is true when the recorded fields are hoisted onto the shot record', () => {
      expect(isBeanRecordedForShot({ beanId: 'bean-1' })).toBe(true);
      expect(isBeanRecordedForShot({ beanType: 'Some Bean' })).toBe(true);
    });

    it('is false when the bean is only a localStorage/inferred guess', () => {
      expect(isBeanRecordedForShot({ profile: 'Espresso', timestamp: 5, notes: {} })).toBe(false);
      expect(isBeanRecordedForShot({})).toBe(false);
      expect(isBeanRecordedForShot(null)).toBe(false);
    });
  });
});
