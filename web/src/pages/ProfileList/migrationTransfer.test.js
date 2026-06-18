import { describe, it, expect } from 'vitest';
import {
  toExportProfile,
  buildExportPayload,
  remapImportedProfile,
  importProfiles,
  formatRestoreSummary,
  aggregateImportFiles,
  formatFileAggregateWarning,
  runRestore,
  MAX_RESTORE_RETRIES,
  ListProfilesError,
} from './migrationTransfer.js';
import { parseProfile } from './utils.js';

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
    ).toBe('Restored 1 of 3 profiles from backup. Failed profiles: Two, Three.');
  });
});

// PRO-218 P2-A: the FINAL alert must reflect the WHOLE picture — profiles AND any
// dropped files — so the last dialog on screen never reads as an unqualified
// success when files/profiles were dropped.
describe('formatRestoreSummary — whole-picture summary with file context (PRO-218 P2-A)', () => {
  it('multi-file clean success: every file contributed, no shortfall, qualified by file counts', () => {
    expect(
      formatRestoreSummary(
        { total: 3, savedCount: 3, failed: [] },
        { selectedFiles: 2, okFiles: 2, failedFiles: [] },
      ),
    ).toBe('Restored 3 of 3 profiles from 2 of 2 files.');
  });

  it('multi-file with DROPPED files: NOT a clean success — names the failed files and says to re-import', () => {
    const msg = formatRestoreSummary(
      { total: 3, savedCount: 3, failed: [] },
      {
        selectedFiles: 5,
        okFiles: 3,
        failedFiles: [{ name: 'corrupt.json' }, { name: 'gone.json' }],
      },
    );
    // It must read the whole picture: profiles restored AND files dropped.
    expect(msg).toBe(
      'Restored 3 of 3 profiles from 3 of 5 files. Failed files: corrupt.json, gone.json. Re-import those files.',
    );
    // Guard: the dropped-files case must never collapse to an unqualified success.
    expect(msg).not.toBe('Restored 3 of 3 profiles from backup.');
    expect(msg).toContain('Failed files');
  });

  it('dropped files AND failed profiles: both shortfalls named in one message', () => {
    const msg = formatRestoreSummary(
      { total: 3, savedCount: 1, failed: [{ label: 'Two' }, { label: 'Three' }] },
      { selectedFiles: 4, okFiles: 2, failedFiles: [{ name: 'bad.json' }] },
    );
    expect(msg).toBe(
      'Restored 1 of 3 profiles from 2 of 4 files. ' +
        'Failed profiles: Two, Three. Failed files: bad.json. Re-import those files.',
    );
  });

  it('single dropped file, all profiles saved: still qualified, not clean success', () => {
    const msg = formatRestoreSummary(
      { total: 1, savedCount: 1, failed: [] },
      { selectedFiles: 2, okFiles: 1, failedFiles: [{ name: 'oops.json' }] },
    );
    expect(msg).toBe(
      'Restored 1 of 1 profile from 1 of 2 files. Failed files: oops.json. Re-import those files.',
    );
  });

  it('zero parsed across a multi-file selection still names the dropped files', () => {
    expect(
      formatRestoreSummary(
        { total: 0, savedCount: 0, failed: [] },
        { selectedFiles: 2, okFiles: 0, failedFiles: [{ name: 'a.json' }, { name: 'b.json' }] },
      ),
    ).toBe(
      'No profiles found in this file — it may be corrupt or the wrong format; nothing was imported. ' +
        'Failed files: a.json, b.json.',
    );
  });
});

describe('aggregateImportFiles — multi-file read+parse+aggregate (PRO-218 NEW-1/NEW-2)', () => {
  // A valid backup file is a JSON array of profiles (what buildExportPayload
  // writes). parseProfile returns the array as-is. A corrupt/unreadable file is
  // either a null text (FileReader failed) or a string that is neither JSON nor
  // valid TCL — parseProfile returns [] for it.
  const goodText = JSON.stringify(buildExportPayload([makeProfile({ id: 'a', label: 'One' })]));
  const goodText2 = JSON.stringify(
    buildExportPayload([
      makeProfile({ id: 'b', label: 'Two' }),
      makeProfile({ id: 'c', label: 'Three' }),
    ]),
  );
  const corruptText = 'this is not json and not a tcl profile @@@ {{{';

  it('all-good: merges every file, no failures, parity against selected files', () => {
    const result = aggregateImportFiles([
      { name: 'one.json', text: goodText },
      { name: 'two.json', text: goodText2 },
    ]);
    expect(result.selectedCount).toBe(2);
    expect(result.okCount).toBe(2);
    expect(result.failedFiles).toHaveLength(0);
    expect(result.profiles.map(p => p.label).sort()).toEqual(['One', 'Three', 'Two']);
    expect(result.fileResults).toEqual([
      { name: 'one.json', ok: true, count: 1 },
      { name: 'two.json', ok: true, count: 2 },
    ]);
  });

  it('some-corrupt: keeps the good files BUT reports the failed ones by name (the NEW-1 fix)', () => {
    const result = aggregateImportFiles([
      { name: 'good.json', text: goodText },
      { name: 'corrupt.json', text: corruptText },
      { name: 'unreadable.json', text: null }, // FileReader onerror -> null
    ]);
    // The two bad files are NOT silently dropped: they surface as failedFiles.
    expect(result.selectedCount).toBe(3);
    expect(result.okCount).toBe(1);
    expect(result.failedFiles.map(f => f.name)).toEqual(['corrupt.json', 'unreadable.json']);
    // The good file's profile still aggregates (survivors are importable).
    expect(result.profiles.map(p => p.label)).toEqual(['One']);
    // Per-file outcome is exact: a corrupt file in a multi-select is visible.
    expect(result.fileResults).toEqual([
      { name: 'good.json', ok: true, count: 1 },
      { name: 'corrupt.json', ok: false, count: 0 },
      { name: 'unreadable.json', ok: false, count: 0 },
    ]);
  });

  it('all-corrupt: zero profiles, every file reported as failed', () => {
    const result = aggregateImportFiles([
      { name: 'a.json', text: corruptText },
      { name: 'b.json', text: null },
    ]);
    expect(result.profiles).toHaveLength(0);
    expect(result.okCount).toBe(0);
    expect(result.failedFiles.map(f => f.name)).toEqual(['a.json', 'b.json']);
  });

  it('empty selection: no files, no profiles, no failures', () => {
    const result = aggregateImportFiles([]);
    expect(result.selectedCount).toBe(0);
    expect(result.profiles).toHaveLength(0);
    expect(result.okCount).toBe(0);
    expect(result.failedFiles).toHaveLength(0);
    expect(result.fileResults).toEqual([]);
  });

  it('tolerates null/undefined input', () => {
    const result = aggregateImportFiles(undefined);
    expect(result.selectedCount).toBe(0);
    expect(result.profiles).toHaveLength(0);
  });
});

describe('formatFileAggregateWarning (PRO-218 NEW-1)', () => {
  it('names the failed files and distinguishes selected vs parsed', () => {
    const aggregate = aggregateImportFiles([
      { name: 'good.json', text: JSON.stringify(buildExportPayload([makeProfile()])) },
      { name: 'corrupt.json', text: 'nope @@@' },
      { name: 'gone.json', text: null },
    ]);
    expect(formatFileAggregateWarning(aggregate)).toBe(
      '2 of 3 selected files could not be read or contained no profiles: ' +
        'corrupt.json, gone.json. Nothing was imported from them.',
    );
  });

  it('uses singular phrasing for a single failed file', () => {
    const aggregate = aggregateImportFiles([{ name: 'only.json', text: 'bad @@@' }]);
    expect(formatFileAggregateWarning(aggregate)).toBe(
      '1 of 1 selected file could not be read or contained no profiles: only.json. ' +
        'Nothing was imported from it.',
    );
  });
});

describe('importProfiles — pre-flight list failure (PRO-218 NEW-3)', () => {
  it('throws a distinct ListProfilesError when the device list fetch rejects, before any save', async () => {
    let saveCalls = 0;
    const adapters = {
      listProfiles: async () => {
        throw new Error('socket closed');
      },
      saveProfile: async profile => {
        saveCalls += 1;
        return { ...profile, id: profile.id || 'minted' };
      },
    };
    await expect(importProfiles(adapters, [makeProfile()])).rejects.toBeInstanceOf(
      ListProfilesError,
    );
    // Nothing was overwritten on the device: the failure is a clean pre-flight abort.
    expect(saveCalls).toBe(0);
  });
});

// PRO-218 P2-B: the onUpload/restore GLUE — the abort/confirm/proceed/retry/give-up
// orchestration that decides what the user sees and how the retry loop is bounded —
// is now extracted into the runRestore() seam and exercised here WITHOUT rendering
// the component. The user-interaction surface (alert/confirm) and the device
// (adapters) are injected, and we assert on the captured messages + outcomes.
describe('runRestore — restore orchestration glue (PRO-218 P2-B)', () => {
  // Capture alert() messages and script confirm() answers so the branch logic is
  // observable without a DOM.
  function makeHarness({ confirmAnswers = [] } = {}) {
    const alerts = [];
    const confirms = [];
    let i = 0;
    return {
      alerts,
      confirms,
      alert: msg => alerts.push(msg),
      confirm: msg => {
        confirms.push(msg);
        // default to "no" once the scripted answers run out (user closes the dialog)
        return i < confirmAnswers.length ? confirmAnswers[i++] : false;
      },
    };
  }

  it('multi-file with some failed FILES: the final summary names them and is NOT a clean success', async () => {
    // All parsed profiles save fine, but two selected files were dropped upstream.
    // The summary the user sees last must fold in the file shortfall (P2-A wired
    // through the glue), never reading as an unqualified "Restored N of N".
    const device = makeDeviceSimulator([]);
    const harness = makeHarness();
    const result = await runRestore({
      adapters: device,
      importedProfiles: [
        makeProfile({ id: 'a', label: 'One' }),
        makeProfile({ id: 'b', label: 'Two' }),
      ],
      fileContext: {
        selectedFiles: 4,
        okFiles: 2,
        failedFiles: [{ name: 'corrupt.json' }, { name: 'gone.json' }],
      },
      alert: harness.alert,
      confirm: harness.confirm,
    });

    expect(result.outcome).toBe('complete');
    expect(result.attempts).toBe(1);
    // Exactly one alert (the summary); no retry confirm because every profile saved.
    expect(harness.alerts).toHaveLength(1);
    expect(harness.alerts[0]).toBe(
      'Restored 2 of 2 profiles from 2 of 4 files. Failed files: corrupt.json, gone.json. Re-import those files.',
    );
    expect(harness.alerts[0]).not.toContain('from backup.');
    expect(harness.confirms).toHaveLength(0);
  });

  it('retries the FAILED SUBSET on confirm and completes when it succeeds the second time', async () => {
    // "Two" fails on the first attempt, succeeds on the retry. The retry batch must
    // be ONLY the failed subset, and the file context must NOT leak into the retry
    // summary (it described the original selection, not the retry batch).
    let pass = 0;
    const device = makeDeviceSimulator([], {
      failOn: profile => profile.label === 'Two' && pass === 0,
    });
    // Flip pass after the first batch so the retry of "Two" lands.
    const harness = makeHarness({ confirmAnswers: [true] });
    const result = await runRestore({
      adapters: device,
      importedProfiles: [
        makeProfile({ id: 'a', label: 'One' }),
        makeProfile({ id: 'b', label: 'Two' }),
      ],
      fileContext: { selectedFiles: 2, okFiles: 2, failedFiles: [] },
      alert: harness.alert,
      confirm: harness.confirm,
      onBeforeRetry: () => {
        pass = 1; // device recovers before the retry
      },
    });

    expect(result.outcome).toBe('complete');
    expect(result.attempts).toBe(2);
    // First summary: partial (file context folds in, but there is a profile shortfall).
    expect(harness.alerts[0]).toContain('Restored 1 of 2 profiles');
    expect(harness.alerts[0]).toContain('Failed profiles: Two.');
    // One retry confirm naming the failed subset.
    expect(harness.confirms).toHaveLength(1);
    expect(harness.confirms[0]).toContain('Two');
    // Second (final) summary: clean success, and the file context did NOT leak.
    expect(harness.alerts[harness.alerts.length - 1]).toBe('Restored 1 of 1 profile from backup.');
  });

  it('stops without retrying when the user declines the retry confirm (partial-stopped)', async () => {
    const device = makeDeviceSimulator([], { failOn: p => p.label === 'Two' });
    const harness = makeHarness({ confirmAnswers: [false] });
    const result = await runRestore({
      adapters: device,
      importedProfiles: [
        makeProfile({ id: 'a', label: 'One' }),
        makeProfile({ id: 'b', label: 'Two' }),
      ],
      alert: harness.alert,
      confirm: harness.confirm,
    });
    expect(result.outcome).toBe('partial-stopped');
    expect(result.attempts).toBe(1);
    expect(harness.confirms).toHaveLength(1);
  });

  it('caps retries at MAX_RESTORE_RETRIES and gives up naming the still-failing subset (NEW-6)', async () => {
    // A permanently-wedged save: every attempt fails. The user keeps clicking retry.
    // The loop must NOT spin forever — it caps at MAX_RESTORE_RETRIES and gives up
    // with a message naming the failed profiles, never silently dropping them.
    const device = makeDeviceSimulator([], { failOn: () => true });
    let beforeRetryCount = 0;
    // Always say "yes" to retry — more than the cap, to prove the cap binds.
    const harness = makeHarness({ confirmAnswers: Array(MAX_RESTORE_RETRIES + 5).fill(true) });
    const result = await runRestore({
      adapters: device,
      importedProfiles: [makeProfile({ id: 'x', label: 'Stuck' })],
      alert: harness.alert,
      confirm: harness.confirm,
      onBeforeRetry: () => {
        beforeRetryCount += 1;
      },
    });

    expect(result.outcome).toBe('gave-up');
    // attempts = the cap + the initial attempt = MAX_RESTORE_RETRIES + 1 import passes.
    expect(result.attempts).toBe(MAX_RESTORE_RETRIES + 1);
    // The confirm was shown exactly MAX_RESTORE_RETRIES times (one per non-final attempt).
    expect(harness.confirms).toHaveLength(MAX_RESTORE_RETRIES);
    // We retried (refetched) once per accepted confirm, never more.
    expect(beforeRetryCount).toBe(MAX_RESTORE_RETRIES);
    // The final alert names the still-failing profile and gives up — not silent.
    const last = harness.alerts[harness.alerts.length - 1];
    expect(last).toContain(`Giving up after ${MAX_RESTORE_RETRIES} retries`);
    expect(last).toContain('Stuck');
  });

  it('respects a lower maxRetries override', async () => {
    const device = makeDeviceSimulator([], { failOn: () => true });
    const harness = makeHarness({ confirmAnswers: Array(10).fill(true) });
    const result = await runRestore({
      adapters: device,
      importedProfiles: [makeProfile({ id: 'x', label: 'Stuck' })],
      alert: harness.alert,
      confirm: harness.confirm,
      maxRetries: 2,
    });
    expect(result.outcome).toBe('gave-up');
    expect(result.attempts).toBe(3); // initial + 2 retries
    expect(harness.confirms).toHaveLength(2);
  });

  it('surfaces a clean abort message when the pre-flight device list read fails (NEW-3)', async () => {
    const adapters = {
      listProfiles: async () => {
        throw new Error('socket closed');
      },
      saveProfile: async () => {
        throw new Error('should not be called');
      },
    };
    const harness = makeHarness();
    const result = await runRestore({
      adapters,
      importedProfiles: [makeProfile()],
      alert: harness.alert,
      confirm: harness.confirm,
    });
    expect(result.outcome).toBe('list-error');
    expect(harness.alerts).toHaveLength(1);
    expect(harness.alerts[0]).toContain('aborted before changing anything');
    expect(harness.confirms).toHaveLength(0);
  });
});

// PRO-218 (parse leniency): a valid-JSON document that is NOT a profile must count
// as an empty/failed parse, so it lands in failedFiles instead of being fed to the
// restore as a bogus profile.
describe('parseProfile — rejects valid-JSON-but-non-profile input (PRO-218 P2 leniency)', () => {
  it('parses a genuine profile array', () => {
    const text = JSON.stringify(buildExportPayload([makeProfile({ id: 'a', label: 'One' })]));
    expect(parseProfile(text).map(p => p.label)).toEqual(['One']);
  });

  it('counts a bare JSON object ({}) as zero profiles', () => {
    expect(parseProfile('{}')).toEqual([]);
  });

  it('counts a valid-JSON-but-unrelated object as zero profiles', () => {
    expect(parseProfile(JSON.stringify({ hello: 'world', count: 3 }))).toEqual([]);
  });

  it('counts a bare JSON primitive as zero profiles', () => {
    expect(parseProfile('42')).toEqual([]);
    expect(parseProfile('"just a string"')).toEqual([]);
    expect(parseProfile('true')).toEqual([]);
  });

  it('filters out non-profile-shaped entries inside an array', () => {
    const mixed = JSON.stringify([makeProfile({ id: 'ok', label: 'Keep' }), { not: 'a profile' }]);
    const parsed = parseProfile(mixed);
    expect(parsed.map(p => p.label)).toEqual(['Keep']);
  });

  it('a non-profile object lands in failedFiles via aggregateImportFiles', () => {
    const result = aggregateImportFiles([
      { name: 'real.json', text: JSON.stringify(buildExportPayload([makeProfile()])) },
      { name: 'random.json', text: JSON.stringify({ unrelated: true }) },
    ]);
    expect(result.okCount).toBe(1);
    expect(result.failedFiles.map(f => f.name)).toEqual(['random.json']);
  });
});
