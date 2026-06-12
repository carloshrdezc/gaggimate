import { beforeEach, describe, expect, it, vi } from 'vitest';
import { listGrinders, recordGrinder } from './grinderManager.js';

const GRINDERS_STORAGE_KEY = 'gaggimate-grinders';
const GRINDERS_PENDING_STORAGE_KEY = 'gaggimate-grinders-pending';

function readKey(key) {
  const raw = localStorage.getItem(key);
  return raw ? JSON.parse(raw) : null;
}

// A fake ApiService whose request() is driven by a queue of handlers. The
// socket appears OPEN so hasConnectedApi() returns true.
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

describe('grinderManager', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  describe('recordGrinder offline tracking', () => {
    it('marks an offline-recorded grinder as pending', async () => {
      const api = makeOfflineApi();
      await recordGrinder(api, 'Niche Zero');

      expect(api.request).not.toHaveBeenCalled();
      expect(readKey(GRINDERS_STORAGE_KEY)).toEqual(['Niche Zero']);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Niche Zero']);
    });

    it('does not mark a successfully-synced grinder as pending', async () => {
      const api = makeApi(async ({ tp, name }) => {
        if (tp === 'req:grinders:save') return { grinders: [name] };
        return { grinders: [] };
      });
      await recordGrinder(api, 'DF64');

      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(readKey(GRINDERS_STORAGE_KEY)).toEqual(['DF64']);
    });

    it('falls back to pending when an online save throws', async () => {
      const api = makeApi(async ({ tp }) => {
        if (tp === 'req:grinders:save') throw new Error('socket drop');
        return { grinders: [] };
      });
      await recordGrinder(api, 'Eureka');

      expect(readKey(GRINDERS_STORAGE_KEY)).toEqual(['Eureka']);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Eureka']);
    });
  });

  describe('listGrinders migration (Finding 1)', () => {
    it('migrates only pending offline names, not stale evicted cache entries', async () => {
      // Cache holds a name the device has evicted ("Old") plus a genuine
      // offline addition ("Offline"). Only the pending one should be pushed.
      localStorage.setItem(GRINDERS_STORAGE_KEY, JSON.stringify(['Offline', 'Old']));
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      const saved = [];
      const api = makeApi(async ({ tp, name }) => {
        if (tp === 'req:grinders:list') return { grinders: ['DeviceA', 'DeviceB'] };
        if (tp === 'req:grinders:save') {
          saved.push(name);
          return { grinders: [name, 'DeviceA', 'DeviceB'] };
        }
        return { grinders: [] };
      });

      const result = await listGrinders(api);

      // "Old" (stale/evicted) must NOT be re-pushed; only "Offline" migrates.
      expect(saved).toEqual(['Offline']);
      // Pending set is cleared once synced.
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result).toEqual(['Offline', 'DeviceA', 'DeviceB']);
    });

    it('does not re-push a pending name the device already has', async () => {
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['DeviceA']));
      const saved = [];
      const api = makeApi(async ({ tp, name }) => {
        if (tp === 'req:grinders:list') return { grinders: ['DeviceA'] };
        if (tp === 'req:grinders:save') {
          saved.push(name);
          return { grinders: ['DeviceA'] };
        }
        return { grinders: [] };
      });

      await listGrinders(api);

      expect(saved).toEqual([]); // already present -> no redundant save
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
    });
  });

  describe('listGrinders retains failed migrations (Finding 2)', () => {
    it('keeps a name pending and in the cache when its save fails', async () => {
      localStorage.setItem(GRINDERS_STORAGE_KEY, JSON.stringify(['Offline']));
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      const api = makeApi(async ({ tp }) => {
        if (tp === 'req:grinders:list') return { grinders: ['DeviceA'] };
        if (tp === 'req:grinders:save') throw new Error('device write error');
        return { grinders: [] };
      });

      const result = await listGrinders(api);

      // The unsynced name survives in both the cache and the pending set.
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Offline']);
      expect(readKey(GRINDERS_STORAGE_KEY)).toContain('Offline');
      expect(result).toContain('Offline');
      expect(result).toContain('DeviceA');
    });

    it('eventually syncs a previously-failed name on a later connected call', async () => {
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      let failNext = true;
      const saved = [];
      const api = makeApi(async ({ tp, name }) => {
        if (tp === 'req:grinders:list') return { grinders: ['DeviceA'] };
        if (tp === 'req:grinders:save') {
          if (failNext) {
            failNext = false;
            throw new Error('transient');
          }
          saved.push(name);
          return { grinders: [name, 'DeviceA'] };
        }
        return { grinders: [] };
      });

      await listGrinders(api); // first attempt fails, stays pending
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Offline']);

      const result = await listGrinders(api); // retry succeeds
      expect(saved).toEqual(['Offline']);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result).toEqual(['Offline', 'DeviceA']);
    });
  });

  describe('offline fallback', () => {
    it('returns the local cache without touching the device when disconnected', async () => {
      localStorage.setItem(GRINDERS_STORAGE_KEY, JSON.stringify(['Cached']));
      const api = makeOfflineApi();
      const result = await listGrinders(api);
      expect(result).toEqual(['Cached']);
      expect(api.request).not.toHaveBeenCalled();
    });
  });
});
