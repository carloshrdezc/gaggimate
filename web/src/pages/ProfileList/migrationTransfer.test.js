import { describe, it, expect } from 'vitest';
import {
  toExportProfile,
  buildExportPayload,
  remapImportedProfile,
  importProfiles,
  formatRestoreSummary,
} from './migrationTransfer.js';

function makeProfile(overrides = {}) {
  return {
    id: 'abc123',
    label: 'Morning Shot',
    type: 'standard',
    temperature: 93,
    phases: [{ name: 'Brew', duration: 28, targets: [{ type: 'volumetric', value: 36 }] }],
    selected: false,
    favorite: false,
    ...overrides,
  };
}

describe('toExportProfile', () => {
  it('strips device-local state (selected/favorite) and keeps everything else', () => {
    const exported = toExportProfile(makeProfile({ selected: true, favorite: true }));
    expect(exported).not.toHaveProperty('selected');
    expect(exported).not.toHaveProperty('favorite');
    expect(exported.id).toBe('abc123');
    expect(exported.label).toBe('Morning Shot');
    expect(exported.phases).toHaveLength(1);
  });

  it('keeps the id so a re-imported profile stays addressable', () => {
    expect(toExportProfile(makeProfile({ id: 'keepme' })).id).toBe('keepme');
  });

  it('tolerates null/undefined input', () => {
    expect(toExportProfile(undefined)).toEqual({});
    expect(toExportProfile(null)).toEqual({});
  });
});

describe('buildExportPayload', () => {
  it('exports every profile with device-local state stripped', () => {
    const profiles = [
      makeProfile({ id: 'p1', label: 'A', selected: true }),
      makeProfile({ id: 'p2', label: 'B', favorite: true }),
    ];
    const payload = buildExportPayload(profiles);
    expect(payload).toHaveLength(2);
    expect(payload.every(p => !('selected' in p) && !('favorite' in p))).toBe(true);
    expect(payload.map(p => p.id)).toEqual(['p1', 'p2']);
  });

  it('returns an empty array for empty/missing input', () => {
    expect(buildExportPayload([])).toEqual([]);
    expect(buildExportPayload(undefined)).toEqual([]);
  });
});

describe('remapImportedProfile', () => {
  it('keeps the id when it does not collide with existing device ids', () => {
    const result = remapImportedProfile(makeProfile({ id: 'fresh' }), new Set(['other']));
    expect(result.id).toBe('fresh');
  });

  it('drops the id when it collides (so the firmware mints a fresh one)', () => {
    const result = remapImportedProfile(makeProfile({ id: 'dupe' }), new Set(['dupe']));
    expect(result).not.toHaveProperty('id');
    expect(result.label).toBe('Morning Shot');
  });

  it('keeps an id-less profile as-is', () => {
    const { id: _omit, ...idless } = makeProfile();
    expect(remapImportedProfile(idless, new Set(['x']))).toEqual(idless);
  });
});

// Device simulator: mirrors the firmware's req:profiles:list / req:profiles:save.
// Exposed as the SAME { listProfiles, saveProfile } adapter shape that the real
// onUpload wires onto apiService.request, so these round-trip tests exercise the
// SHIPPED importProfiles() orchestrator rather than a hand-copied loop (PRO-218
// P1-2). `echoId: false` simulates a firmware/transport that fails to echo the
// stored id, used to pin the dedup contract (PRO-218 P2-1).
function makeDeviceSimulator(initialProfiles = [], { echoId = true, failOn = () => false } = {}) {
  const store = new Map(initialProfiles.map(p => [p.id, { ...p }]));
  let mintCounter = 0;
  return {
    listProfiles: async () => [...store.values()].map(p => ({ ...p })),
    saveProfile: async profile => {
      if (failOn(profile)) {
        throw new Error('simulated save failure');
      }
      const id = profile.id || `minted${++mintCounter}`;
      const stored = { ...profile, id };
      store.set(id, stored);
      // mirrors req:profiles:save echoing the stored profile (incl. minted id)
      return echoId ? { ...stored } : { ...stored, id: undefined };
    },
    // test-only inspection
    _list: () => [...store.values()].map(p => ({ ...p })),
  };
}

describe('importProfiles — the shipped restore orchestrator (SPIFFS->LittleFS migration)', () => {
  it('restores all profiles onto a freshly-formatted device (only Default present)', async () => {
    // 1. Source device with the user's profiles + a selected/favorited one.
    const source = [
      makeProfile({ id: 'esp1', label: 'Espresso', favorite: true, selected: true }),
      makeProfile({ id: 'ris1', label: 'Ristretto' }),
      makeProfile({ id: 'fil1', label: 'Filter', type: 'pro' }),
    ];

    // 2. Export off-device (what gets written to profiles.json).
    const exported = buildExportPayload(source);
    const fileContents = JSON.stringify(exported, undefined, 2);

    // 3. Fresh LittleFS device after clean format: only the seeded Default.
    const device = makeDeviceSimulator([{ id: 'def0', label: 'Default', type: 'standard' }]);

    // 4. Re-import the file through the SHIPPED orchestrator.
    const reparsed = JSON.parse(fileContents);
    const summary = await importProfiles(device, reparsed);

    // 5. All three user profiles are present alongside Default; none clobbered,
    //    none lost. Non-colliding ids are preserved verbatim.
    expect(summary.savedCount).toBe(3);
    expect(summary.total).toBe(3);
    expect(summary.failed).toHaveLength(0);

    const labels = device
      ._list()
      .map(p => p.label)
      .sort();
    expect(labels).toEqual(['Default', 'Espresso', 'Filter', 'Ristretto']);
    const espresso = device._list().find(p => p.label === 'Espresso');
    expect(espresso.id).toBe('esp1');
    // device-local state was not round-tripped
    expect(espresso).not.toHaveProperty('selected');
    expect(espresso).not.toHaveProperty('favorite');
    // payload content survived
    expect(device._list().find(p => p.label === 'Filter').type).toBe('pro');
  });

  it('remaps a colliding import instead of overwriting the device copy', async () => {
    // Device already has a profile with id "esp1" (a newer edit).
    const device = makeDeviceSimulator([{ id: 'esp1', label: 'Device Espresso v2' }]);
    const imported = buildExportPayload([makeProfile({ id: 'esp1', label: 'Backup Espresso' })]);

    const summary = await importProfiles(device, imported);
    expect(summary.savedCount).toBe(1);

    // The device copy is untouched; the import landed under a minted id.
    const byLabel = Object.fromEntries(device._list().map(p => [p.label, p]));
    expect(byLabel['Device Espresso v2'].id).toBe('esp1');
    expect(byLabel['Backup Espresso'].id).not.toBe('esp1');
    expect(device._list()).toHaveLength(2);
  });

  it('does not let two same-id profiles within one file overwrite each other', async () => {
    const device = makeDeviceSimulator([]);
    // A malformed/duplicated backup: two entries claiming the same id.
    const summary = await importProfiles(device, [
      makeProfile({ id: 'same', label: 'First' }),
      makeProfile({ id: 'same', label: 'Second' }),
    ]);
    expect(summary.savedCount).toBe(2);

    const list = device._list();
    expect(list).toHaveLength(2);
    expect(new Set(list.map(p => p.id)).size).toBe(2);
    expect(list.map(p => p.label).sort()).toEqual(['First', 'Second']);
  });

  it('continues past a failed save and reports a partial restore (PRO-218 P1-1)', async () => {
    // The middle profile fails to save (e.g. firmware reject or a hung/timed-out
    // request). The loop must not abort: the third profile still lands, and the
    // failure is reported by label.
    const device = makeDeviceSimulator([], {
      failOn: profile => profile.label === 'Two',
    });
    const summary = await importProfiles(device, [
      makeProfile({ id: 'a', label: 'One' }),
      makeProfile({ id: 'b', label: 'Two' }),
      makeProfile({ id: 'c', label: 'Three' }),
    ]);

    expect(summary.total).toBe(3);
    expect(summary.savedCount).toBe(2);
    expect(summary.failed.map(f => f.label)).toEqual(['Two']);
    // the failed item carries its original profile for a retry of just that subset
    expect(summary.failed[0].profile.label).toBe('Two');
    // the profiles after the failure were still restored
    expect(
      device
        ._list()
        .map(p => p.label)
        .sort(),
    ).toEqual(['One', 'Three']);
  });

  it('treats a missing echoed id as a hard per-item failure, not silent continue (PRO-218 P2-1)', async () => {
    // The intra-batch dedup relies on the firmware echoing the stored id back so
    // a SECOND same-id profile also remaps. If the contract is broken (no id
    // echoed), continuing silently would let the next same-id profile clobber the
    // first. importProfiles must flag it instead. The simulator here always omits
    // the id, falsifying the branch the always-echo simulator can never reach.
    const device = makeDeviceSimulator([], { echoId: false });
    const summary = await importProfiles(device, [makeProfile({ id: 'x', label: 'NoEcho' })]);

    expect(summary.savedCount).toBe(0);
    expect(summary.failed).toHaveLength(1);
    expect(summary.failed[0].label).toBe('NoEcho');
    expect(summary.failed[0].error).toBeInstanceOf(Error);
  });

  it('reports count parity for a fully-successful restore (PRO-218 P2-2)', async () => {
    const device = makeDeviceSimulator([]);
    const summary = await importProfiles(device, [
      makeProfile({ id: 'a', label: 'One' }),
      makeProfile({ id: 'b', label: 'Two' }),
    ]);
    expect(formatRestoreSummary(summary)).toBe('Restored 2 of 2 profiles from backup.');
  });
});

describe('formatRestoreSummary', () => {
  it('reports a distinct corrupt-file error for an empty parse (PRO-218 P0-1)', () => {
    expect(formatRestoreSummary({ total: 0, savedCount: 0, failed: [] })).toBe(
      'No profiles found in this file — it may be corrupt or the wrong format; nothing was imported.',
    );
  });

  it('reports full success with singular/plural agreement', () => {
    expect(formatRestoreSummary({ total: 1, savedCount: 1, failed: [] })).toBe(
      'Restored 1 of 1 profile from backup.',
    );
    expect(formatRestoreSummary({ total: 3, savedCount: 3, failed: [] })).toBe(
      'Restored 3 of 3 profiles from backup.',
    );
  });

  it('names the failed profiles on a partial restore', () => {
    expect(
      formatRestoreSummary({
        total: 3,
        savedCount: 1,
        failed: [{ label: 'Two' }, { label: 'Three' }],
      }),
    ).toBe('Restored 1 of 3 profiles from backup — failed: Two, Three.');
  });
});
