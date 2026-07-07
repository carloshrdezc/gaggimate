import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  listGrinders,
  recordGrinder,
  recordGrinderSelection,
  recordManualGrindSetting,
  getCurrentGrinderSelection,
  inferGrinderForShot,
  inferGrindSettingForShot,
} from './grinderManager.js';

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

// A device simulator that mirrors the firmware's authoritative merge/dedup/cap:
// prepend incoming names most-recently-first (the LAST element of `names` ends
// up at the very front), dedup case-insensitively, cap to `cap`. Tracks each
// save's payload so tests can assert what was sent. Handles both the single
// `name` (back-compat) and batch `names` shapes.
function makeDevice({ initial = [], cap = 50 } = {}) {
  let list = [...initial];
  const saves = [];

  const merge = names => {
    // Pre-dedup the batch case-insensitively keeping the FIRST occurrence's
    // casing/position — matching the firmware's recordGrinders() (which dedups
    // the batch before prepending). Then apply per-name "remove existing match,
    // prepend" so the last surviving element ends up front-most.
    const seen = new Set();
    const deduped = [];
    for (const raw of names) {
      const name = String(raw || '').trim();
      if (!name) continue;
      const key = name.toLowerCase();
      if (seen.has(key)) continue;
      seen.add(key);
      deduped.push(name);
    }
    for (const name of deduped) {
      list = [name, ...list.filter(n => n.toLowerCase() !== name.toLowerCase())].slice(0, cap);
    }
    return list.slice();
  };

  const handler = async ({ tp, name, names }) => {
    if (tp === 'req:grinders:list') return { grinders: list.slice() };
    if (tp === 'req:grinders:save') {
      const batch = Array.isArray(names) ? names : [name];
      saves.push(batch);
      return { grinders: merge(batch) };
    }
    return { grinders: [] };
  };

  return { api: makeApi(handler), saves, current: () => list.slice() };
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

    it('rejects a name over 64 UTF-8 bytes even when under 64 UTF-16 units', async () => {
      // The firmware caps names at 64 *bytes* (Arduino String::length()), but
      // JS String.length counts UTF-16 code units. 40 accented chars = 40
      // UTF-16 units (would pass a naive .length check) but 80 UTF-8 bytes — the
      // firmware would reject it on every save, so it must never enter
      // pending/cache in the first place.
      const name = 'é'.repeat(40); // 40 UTF-16 units, 80 UTF-8 bytes
      expect(name.length).toBe(40);
      expect(new TextEncoder().encode(name).length).toBe(80);

      const api = makeOfflineApi();
      await recordGrinder(api, name);

      expect(readKey(GRINDERS_STORAGE_KEY) || []).toEqual([]);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
    });

    it('accepts a multibyte name that fits within 64 UTF-8 bytes', async () => {
      const name = 'é'.repeat(30); // 30 UTF-16 units, 60 UTF-8 bytes -> OK
      expect(new TextEncoder().encode(name).length).toBe(60);

      const api = makeOfflineApi();
      await recordGrinder(api, name);

      expect(readKey(GRINDERS_STORAGE_KEY)).toEqual([name]);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual([name]);
    });

    it('does not mark a successfully-synced grinder as pending', async () => {
      const { api } = makeDevice();
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

  describe('listGrinders batch migration (device-authoritative)', () => {
    it('migrates only pending offline names, not stale evicted cache entries', async () => {
      // Cache holds a name the device has evicted ("Old") plus a genuine
      // offline addition ("Offline"). Only the pending one is sent; the device
      // returns the authoritative list.
      localStorage.setItem(GRINDERS_STORAGE_KEY, JSON.stringify(['Offline', 'Old']));
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      const { api, saves } = makeDevice({ initial: ['DeviceA', 'DeviceB'] });

      const result = await listGrinders(api);

      // Only the pending "Offline" is sent (one batch); "Old" is never pushed.
      expect(saves).toEqual([['Offline']]);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result).toEqual(['Offline', 'DeviceA', 'DeviceB']);
    });

    it('still clears pending for a name the device already has', async () => {
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['DeviceA']));
      const { api } = makeDevice({ initial: ['DeviceA'] });

      const result = await listGrinders(api);

      // Pending resolved; device list is authoritative and unchanged.
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      expect(result).toEqual(['DeviceA']);
    });

    it('sends all pending names in a SINGLE batch (oldest-first)', async () => {
      // pending is most-recent-first ['B','A']; the client sends them
      // oldest-first so the device ends with the most-recent ('B') at front.
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['B', 'A']));
      const { api, saves } = makeDevice({ initial: ['X'] });

      const result = await listGrinders(api);

      // Exactly one save call carrying both names oldest-first.
      expect(saves).toEqual([['A', 'B']]);
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      // Device prepended A then B -> B is front-most; X retained.
      expect(result).toEqual(['B', 'A', 'X']);
    });

    it('does not lose pending names when the device list is at its cap', async () => {
      // The whole point of the refactor: the client no longer models eviction.
      // Device cap=2 starting full; two pending names must both reach the device
      // (the device decides what the final two are), and pending is cleared.
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['New2', 'New1']));
      const { api, saves } = makeDevice({ initial: ['Old1', 'Old2'], cap: 2 });

      const result = await listGrinders(api);

      // One batch, both names sent oldest-first.
      expect(saves).toEqual([['New1', 'New2']]);
      // Pending fully resolved — the device's returned list is authoritative.
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY) || []).toEqual([]);
      // Device merged + capped to 2, most-recent-first.
      expect(result).toEqual(['New2', 'New1']);
      // Cache mirrors the device list exactly.
      expect(readKey(GRINDERS_STORAGE_KEY)).toEqual(['New2', 'New1']);
    });
  });

  describe('listGrinders retains failed migrations', () => {
    it('keeps names pending and in the cache when the batch save fails', async () => {
      localStorage.setItem(GRINDERS_STORAGE_KEY, JSON.stringify(['Offline']));
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      const api = makeApi(async ({ tp }) => {
        if (tp === 'req:grinders:list') return { grinders: ['DeviceA'] };
        if (tp === 'req:grinders:save') throw new Error('device write error');
        return { grinders: [] };
      });

      const result = await listGrinders(api);

      // The unsynced name survives in both the cache and the pending set, and
      // is merged on top of the last-known device list for the UI.
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Offline']);
      expect(readKey(GRINDERS_STORAGE_KEY)).toContain('Offline');
      expect(result).toContain('Offline');
      expect(result).toContain('DeviceA');
    });

    it('eventually syncs a previously-failed batch on a later connected call', async () => {
      localStorage.setItem(GRINDERS_PENDING_STORAGE_KEY, JSON.stringify(['Offline']));

      let failNext = true;
      const saves = [];
      let device = ['DeviceA'];
      const api = makeApi(async ({ tp, name, names }) => {
        if (tp === 'req:grinders:list') return { grinders: device.slice() };
        if (tp === 'req:grinders:save') {
          if (failNext) {
            failNext = false;
            throw new Error('transient');
          }
          const batch = Array.isArray(names) ? names : [name];
          saves.push(batch);
          for (const n of batch) {
            device = [n, ...device.filter(x => x.toLowerCase() !== n.toLowerCase())];
          }
          return { grinders: device.slice() };
        }
        return { grinders: [] };
      });

      await listGrinders(api); // first batch fails, stays pending
      expect(readKey(GRINDERS_PENDING_STORAGE_KEY)).toEqual(['Offline']);

      const result = await listGrinders(api); // retry succeeds
      expect(saves).toEqual([['Offline']]);
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

  // PRO-425: dashboard grinder-selection log used to pre-fill Shot Notes.
  describe('grinder selection events (PRO-425)', () => {
    it('records a selection and exposes it as the current selection', () => {
      const evt = recordGrinderSelection({
        profileId: 'p1',
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '18.0g',
      });
      expect(evt).toMatchObject({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '18.0g',
      });
      expect(getCurrentGrinderSelection()).toMatchObject({ grinder: 'Niche Zero' });
    });

    it('no-ops for a blank grinder name', () => {
      expect(recordGrinderSelection({ grinder: '   ' })).toBeNull();
      expect(getCurrentGrinderSelection()).toBeNull();
    });

    it('infers grinder + grindSetting for a later shot of the same profile', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
      // Shot timestamp is in Unix SECONDS; selection is in ms. Use a shot pulled
      // just after the selection.
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrinderForShot(shot)).toBe('Niche Zero');
      expect(inferGrindSettingForShot(shot)).toBe('28s');
    });

    it('does not infer a selection recorded AFTER the shot was pulled', () => {
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) - 60 };
      expect(inferGrinderForShot(shot)).toBe('');
      expect(inferGrindSettingForShot(shot)).toBe('');
    });

    it('prefers an explicit value on the shot / its notes over the selection log', () => {
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grinder: 'Saved Grinder', grindSetting: 'Saved Setting' },
      };
      expect(inferGrinderForShot(shot)).toBe('Saved Grinder');
      expect(inferGrindSettingForShot(shot)).toBe('Saved Setting');
    });

    it('does not cross profiles', () => {
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });
      const shot = { profile: 'Filter', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrinderForShot(shot)).toBe('');
    });
  });

  // PRO-431: manual grinder-dial setting typed on the dashboard, pre-filled into
  // Shot Notes with precedence over the machine grind-TARGET label.
  describe('manual grind-setting (PRO-431)', () => {
    it('records a manual grind setting and infers it for a later shot of the same profile', () => {
      const evt = recordManualGrindSetting({
        profileId: 'p1',
        profileLabel: 'Espresso',
        grindSetting: '2.5',
      });
      expect(evt).toMatchObject({ profileLabel: 'Espresso', grindSetting: '2.5' });

      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('2.5');
    });

    it('no-ops for a blank / whitespace value', () => {
      expect(recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '   ' })).toBeNull();
      expect(recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '' })).toBeNull();
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('');
    });

    it('takes PRECEDENCE over the machine grind-TARGET label from the selection log', () => {
      // Machine target label recorded via the grinder-selection log …
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });
      // … and a manual dial number recorded afterwards. The manual value wins.
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });

      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('wins even when the machine target was recorded AFTER the manual value', () => {
      // Precedence is by SOURCE (manual > machine target), not merely recency:
      // a machine-target selection recorded after the manual entry must not
      // shadow it.
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });

      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('falls back to the machine target label when no manual value was entered', () => {
      recordGrinderSelection({ profileLabel: 'Espresso', grinder: 'Niche Zero', grindSetting: '28s' });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('28s');
    });

    it('never clobbers a saved note or an explicit shot value', () => {
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      const shotWithNote = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grindSetting: 'Saved Setting' },
      };
      expect(inferGrindSettingForShot(shotWithNote)).toBe('Saved Setting');

      const shotWithExplicit = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        grindSetting: 'Explicit Setting',
      };
      expect(inferGrindSettingForShot(shotWithExplicit)).toBe('Explicit Setting');
    });

    it('does not infer a manual value recorded AFTER the shot was pulled', () => {
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) - 60 };
      expect(inferGrindSettingForShot(shot)).toBe('');
    });

    it('does not cross profiles', () => {
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      const shot = { profile: 'Filter', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('');
    });
  });
});
