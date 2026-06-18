import { describe, it, expect } from 'vitest';
import { toExportProfile, buildExportPayload, remapImportedProfile } from './migrationTransfer.js';

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

describe('export -> import round-trip (the SPIFFS->LittleFS migration story)', () => {
  // Simulate the firmware: req:profiles:save persists the profile, minting a
  // fresh short id when the import arrives without one.
  function makeDeviceSimulator(initialProfiles = []) {
    const store = new Map(initialProfiles.map(p => [p.id, { ...p }]));
    let mintCounter = 0;
    return {
      // mirrors req:profiles:list -> reports the in-file id
      list: () => [...store.values()].map(p => ({ ...p })),
      // mirrors req:profiles:save -> echoes the stored profile incl. minted id
      save: profile => {
        const id = profile.id || `minted${++mintCounter}`;
        const stored = { ...profile, id };
        store.set(id, stored);
        return { profile: { ...stored } };
      },
    };
  }

  // mirrors the onUpload loop in index.jsx (id-collision remap + batch tracking)
  function importInto(device, importedProfiles) {
    const existingIds = new Set(
      device
        .list()
        .map(p => p.id)
        .filter(Boolean),
    );
    for (const p of importedProfiles) {
      const profile = remapImportedProfile(p, existingIds);
      const savedId = device.save(profile)?.profile?.id;
      if (savedId) existingIds.add(savedId);
    }
  }

  it('restores all profiles onto a freshly-formatted device (only Default present)', () => {
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

    // 4. Re-import the file.
    const reparsed = JSON.parse(fileContents);
    importInto(device, reparsed);

    // 5. All three user profiles are present alongside Default; none clobbered,
    //    none lost. Non-colliding ids are preserved verbatim.
    const labels = device
      .list()
      .map(p => p.label)
      .sort();
    expect(labels).toEqual(['Default', 'Espresso', 'Filter', 'Ristretto']);
    const espresso = device.list().find(p => p.label === 'Espresso');
    expect(espresso.id).toBe('esp1');
    // device-local state was not round-tripped
    expect(espresso).not.toHaveProperty('selected');
    expect(espresso).not.toHaveProperty('favorite');
    // payload content survived
    expect(device.list().find(p => p.label === 'Filter').type).toBe('pro');
  });

  it('remaps a colliding import instead of overwriting the device copy', () => {
    // Device already has a profile with id "esp1" (a newer edit).
    const device = makeDeviceSimulator([{ id: 'esp1', label: 'Device Espresso v2' }]);
    const imported = buildExportPayload([makeProfile({ id: 'esp1', label: 'Backup Espresso' })]);

    importInto(device, imported);

    // The device copy is untouched; the import landed under a minted id.
    const byLabel = Object.fromEntries(device.list().map(p => [p.label, p]));
    expect(byLabel['Device Espresso v2'].id).toBe('esp1');
    expect(byLabel['Backup Espresso'].id).not.toBe('esp1');
    expect(device.list()).toHaveLength(2);
  });

  it('does not let two same-id profiles within one file overwrite each other', () => {
    const device = makeDeviceSimulator([]);
    // A malformed/duplicated backup: two entries claiming the same id.
    importInto(device, [
      makeProfile({ id: 'same', label: 'First' }),
      makeProfile({ id: 'same', label: 'Second' }),
    ]);

    const list = device.list();
    expect(list).toHaveLength(2);
    expect(new Set(list.map(p => p.id)).size).toBe(2);
    expect(list.map(p => p.label).sort()).toEqual(['First', 'Second']);
  });
});
