import { describe, expect, it } from 'vitest';
import {
  getProfileImportConflicts,
  parseProfileImportText,
  planProfileImports,
  serializeProfileForExport,
  serializeProfilesForExport,
} from './profileTransfer.js';

function makeProfile(overrides = {}) {
  return {
    id: '11111111-1111-4111-8111-111111111111',
    label: 'Flat White',
    type: 'pro',
    description: '',
    temperature: 93,
    favorite: true,
    selected: true,
    utility: false,
    phases: [
      {
        name: 'Phase 1',
        phase: 'brew',
        valve: 1,
        duration: 10,
        pump: { target: 'pressure', pressure: 6, flow: 0 },
        transition: { type: 'instant', duration: 0, adaptive: false },
      },
    ],
    ...overrides,
  };
}

describe('profileTransfer', () => {
  it('strips transient fields when exporting a single profile', () => {
    const exported = serializeProfileForExport(makeProfile());
    expect(exported).not.toHaveProperty('id');
    expect(exported).not.toHaveProperty('favorite');
    expect(exported).not.toHaveProperty('selected');
    expect(exported.utility).toBe(false);
  });

  it('strips transient fields when exporting a profile list', () => {
    const exported = serializeProfilesForExport([makeProfile({ utility: true })]);
    expect(exported).toHaveLength(1);
    expect(exported[0]).not.toHaveProperty('id');
    expect(exported[0]).toMatchObject({ label: 'Flat White', utility: true });
  });

  it('parses a JSON profile array and normalizes defaults', () => {
    const { source, profiles } = parseProfileImportText(JSON.stringify([makeProfile({ id: undefined, favorite: undefined, selected: undefined })]));
    expect(source).toBe('json');
    expect(profiles).toHaveLength(1);
    expect(profiles[0].label).toBe('Flat White');
    expect(profiles[0]).not.toHaveProperty('id');
    expect(profiles[0].favorite).toBe(false);
    expect(profiles[0].selected).toBe(false);
    expect(profiles[0].utility).toBe(false);
  });

  it('rejects malformed JSON with a readable error', () => {
    expect(() => parseProfileImportText('{"label": "Broken"')).toThrow(/Invalid JSON/);
  });

  it('detects label conflicts against existing profiles', () => {
    const conflicts = getProfileImportConflicts([makeProfile({ id: undefined })], [makeProfile()]);
    expect(conflicts).toHaveLength(1);
    expect(conflicts[0].existing.label).toBe('Flat White');
  });

  it('plans overwrites by reusing the existing id', () => {
    const existing = makeProfile({ id: '22222222-2222-4222-8222-222222222222', favorite: false, selected: false });
    const planned = planProfileImports([makeProfile({ id: undefined, description: 'updated' })], [existing], {
      mode: 'overwrite',
    });
    expect(planned).toHaveLength(1);
    expect(planned[0].id).toBe(existing.id);
    expect(planned[0].description).toBe('updated');
  });

  it('renames imported labels when keeping both profiles', () => {
    const existing = makeProfile();
    const planned = planProfileImports([makeProfile({ id: undefined })], [existing], {
      mode: 'rename',
    });
    expect(planned).toHaveLength(1);
    expect(planned[0].label).toMatch(/Flat White \(Imported\)/);
    expect(planned[0].id).toBeTruthy();
    expect(planned[0].id).not.toBe(existing.id);
  });
});
