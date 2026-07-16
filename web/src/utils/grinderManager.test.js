import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  listGrinders,
  recordGrinder,
  recordGrinderSelection,
  recordManualGrindSetting,
  getCurrentGrinderSelection,
  inferGrinderForShot,
  inferGrindSettingForShot,
  isGrinderRecordedForShot,
  isGrindTargetRecordedForShot,
  resolveGrinderPrefill,
  MANUAL_GRIND_SETTING_STORAGE_KEY,
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
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) - 60 };
      expect(inferGrinderForShot(shot)).toBe('');
      expect(inferGrindSettingForShot(shot)).toBe('');
    });

    it('prefers an explicit value on the shot / its notes over the selection log', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grinder: 'Saved Grinder', grindSetting: 'Saved Setting' },
      };
      expect(inferGrinderForShot(shot)).toBe('Saved Grinder');
      expect(inferGrindSettingForShot(shot)).toBe('Saved Setting');
    });

    it('does not cross profiles', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
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

    // PRO-503: EditableNumBlock's onClick fires onTap (which re-commits the
    // current value) BEFORE entering edit mode, so tapping the GRIND field
    // and tapping away WITHOUT typing/pressing Enter still records a fresh
    // event. This pins the function-level contract this depends on: calling
    // recordManualGrindSetting again with the SAME grindSetting must produce
    // a new, later event rather than being a no-op or being deduped away —
    // the fix relies on this to make a re-commit of an unchanged value count.
    it('records a fresh event when the same grindSetting is re-committed (tap-away without Enter)', async () => {
      const first = recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      // Ensure a distinguishable timestamp between the two events.
      await new Promise(resolve => setTimeout(resolve, 2));
      const second = recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });

      expect(first).not.toBeNull();
      expect(second).not.toBeNull();
      expect(second.id).not.toBe(first.id);
      expect(second.selectedAtMs).toBeGreaterThanOrEqual(first.selectedAtMs);

      // A shot pulled after the re-commit still resolves the (unchanged) value.
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('no-ops for a blank / whitespace value', () => {
      expect(
        recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '   ' }),
      ).toBeNull();
      expect(recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '' })).toBeNull();
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('');
    });

    it('takes PRECEDENCE over the machine grind-TARGET label from the selection log', () => {
      // Machine target label recorded via the grinder-selection log …
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
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
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });

      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('falls back to the machine target label when no manual value was entered', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'Niche Zero',
        grindSetting: '28s',
      });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrindSettingForShot(shot)).toBe('28s');
    });

    it('uses the current Dashboard manual setting over the device grindTarget when no fresh event exists', () => {
      localStorage.setItem(MANUAL_GRIND_SETTING_STORAGE_KEY, '3.2');
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grindTarget: '25s' },
      };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('never clobbers a saved note or an explicit shot value', () => {
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      localStorage.setItem(MANUAL_GRIND_SETTING_STORAGE_KEY, '4.1');
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

    it.each(['0', '', 'not-a-number', '-1'])('falls back to the machine target when Dashboard manual setting is %s', value => {
      localStorage.setItem(MANUAL_GRIND_SETTING_STORAGE_KEY, value);
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grindTarget: '25s' },
      };
      expect(inferGrindSettingForShot(shot)).toBe('25s');
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

  // PRO-430: the device now stamps the selected grinder NAME into a shot's notes
  // under `grinderName` (PRO-428 firmware contract). isGrinderRecordedForShot
  // distinguishes that authoritative device value from a per-browser
  // localStorage selection-event guess, and inferGrinderForShot must prefer it.
  describe('isGrinderRecordedForShot (PRO-430)', () => {
    it('is true when the shot notes carry a device-recorded grinderName', () => {
      expect(isGrinderRecordedForShot({ notes: { grinderName: 'Niche Zero' } })).toBe(true);
    });

    it('is true when the recorded fields are hoisted onto the shot record', () => {
      expect(isGrinderRecordedForShot({ grinderName: 'Niche Zero' })).toBe(true);
    });

    it('is true when a user-entered grinder note is present', () => {
      expect(isGrinderRecordedForShot({ notes: { grinder: 'Saved Grinder' } })).toBe(true);
      expect(isGrinderRecordedForShot({ grinder: 'Saved Grinder' })).toBe(true);
    });

    it('is false when the grinder is only a localStorage/inferred guess', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'LStore Guess',
        grindSetting: '28s',
      });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5, notes: {} };
      // A localStorage selection event WOULD infer for this shot, but no device
      // field is present, so it is not authoritative.
      expect(inferGrinderForShot(shot)).toBe('LStore Guess');
      expect(isGrinderRecordedForShot(shot)).toBe(false);
      expect(isGrinderRecordedForShot({})).toBe(false);
      expect(isGrinderRecordedForShot(null)).toBe(false);
    });
  });

  describe('inferGrinderForShot device-recorded precedence (PRO-430)', () => {
    it('prefers the device-recorded notes.grinderName over a different localStorage-log grinder', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'LStore Guess',
        grindSetting: '28s',
      });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grinderName: 'Device Niche' },
      };
      expect(inferGrinderForShot(shot)).toBe('Device Niche');
    });

    it('prefers a hoisted shot.grinderName over the localStorage log', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'LStore Guess',
        grindSetting: '28s',
      });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        grinderName: 'Device Niche',
      };
      expect(inferGrinderForShot(shot)).toBe('Device Niche');
    });

    it('still resolves a localStorage-only shot from the selection log (no regression)', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'LStore Guess',
        grindSetting: '28s',
      });
      const shot = { profile: 'Espresso', timestamp: Math.floor(Date.now() / 1000) + 5 };
      expect(inferGrinderForShot(shot)).toBe('LStore Guess');
    });
  });

  // PRO-438: the Shot Notes form field binds to `grinder`, but the device
  // firmware stamps the selected grinder under `grinderName` (PRO-428). The
  // ShotNotesCard prefill dropped it (grinder blank in a fresh browser).
  // resolveGrinderPrefill surfaces the device value as an editable default,
  // fill-only-when-empty so a saved grinder is never clobbered.
  // Precedence: saved loadedNotes.grinder > device savedNotes.grinderName >
  // localStorage selection-log guess shot.grinder.
  describe('resolveGrinderPrefill (PRO-438)', () => {
    it('keeps a saved user-entered grinder over the device grinderName', () => {
      const loadedNotes = { grinder: 'My Saved Grinder' };
      const savedNotes = { grinderName: 'Device Df64' };
      const shot = { grinder: 'LStore Guess' };
      expect(resolveGrinderPrefill(loadedNotes, savedNotes, shot)).toBe('My Saved Grinder');
    });

    it('uses the device savedNotes.grinderName when no saved grinder exists', () => {
      const loadedNotes = { grinder: '' };
      const savedNotes = { grinderName: 'Device Df64' };
      const shot = { grinder: 'LStore Guess' };
      expect(resolveGrinderPrefill(loadedNotes, savedNotes, shot)).toBe('Device Df64');
    });

    it('falls back to the localStorage shot.grinder guess when neither is present', () => {
      const loadedNotes = { grinder: '' };
      const savedNotes = {};
      const shot = { grinder: 'LStore Guess' };
      expect(resolveGrinderPrefill(loadedNotes, savedNotes, shot)).toBe('LStore Guess');
    });

    it('returns an empty string when no source has a grinder', () => {
      expect(resolveGrinderPrefill({ grinder: '' }, {}, {})).toBe('');
    });

    it('returns empty string when all args are undefined (null-safety)', () => {
      expect(resolveGrinderPrefill(undefined, undefined, undefined)).toBe('');
    });

    it('uses savedNotes.grinderName when shot has no grinder and loadedNotes has no truthy grinder', () => {
      const loadedNotes = { grinder: '' };
      const savedNotes = { grinderName: 'Device Df64' };
      const shot = {};
      expect(resolveGrinderPrefill(loadedNotes, savedNotes, shot)).toBe('Device Df64');
    });

    it('treats a whitespace-only loadedNotes.grinder as empty so device grinderName wins', () => {
      const loadedNotes = { grinder: '   ' };
      const savedNotes = { grinderName: 'Device Df64' };
      const shot = { grinder: 'LStore Guess' };
      expect(resolveGrinderPrefill(loadedNotes, savedNotes, shot)).toBe('Device Df64');
    });
  });

  // PRO-441: the DEVICE stamps the machine grind TARGET (auto-grind grams/seconds)
  // onto shot notes as `grindTarget` (a display label). It is device-authoritative
  // (reads the same in every browser) and DISTINCT from the user-editable
  // grindSetting dial. inferGrindSettingForShot prefers it over the per-browser
  // localStorage selection-log fallback, but a real manual dial number still wins.
  describe('device grind TARGET on shot notes (PRO-441)', () => {
    it('prefers the device-recorded notes.grindTarget over the localStorage selection-log fallback', () => {
      recordGrinderSelection({
        profileLabel: 'Espresso',
        grinder: 'LStore Guess',
        grindSetting: '28s',
      });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grindTarget: '18.0g' },
      };
      // The selection log WOULD infer '28s', but the device target is authoritative.
      expect(inferGrindSettingForShot(shot)).toBe('18.0g');
    });

    it('lets a MANUAL dial value outrank the device grindTarget (no regression)', () => {
      recordManualGrindSetting({ profileLabel: 'Espresso', grindSetting: '3.2' });
      const shot = {
        profile: 'Espresso',
        timestamp: Math.floor(Date.now() / 1000) + 5,
        notes: { grindTarget: '18.0g' },
      };
      expect(inferGrindSettingForShot(shot)).toBe('3.2');
    });

    it('isGrindTargetRecordedForShot is true only when notes.grindTarget is set', () => {
      expect(isGrindTargetRecordedForShot({ notes: { grindTarget: '25s' } })).toBe(true);
      expect(isGrindTargetRecordedForShot({ notes: { grindSetting: '3.2' } })).toBe(false);
      expect(isGrindTargetRecordedForShot({ notes: {} })).toBe(false);
      expect(isGrindTargetRecordedForShot({})).toBe(false);
      expect(isGrindTargetRecordedForShot(null)).toBe(false);
    });
  });
});
