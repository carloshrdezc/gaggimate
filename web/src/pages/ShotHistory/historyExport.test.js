import { describe, test, expect, vi, beforeEach } from 'vitest';

vi.mock('../../utils/download.js', () => ({
  downloadBlob: vi.fn(),
}));

import { downloadBlob } from '../../utils/download.js';
import { exportShotsAsCsv } from './historyExport.js';

async function csvFromLastCall() {
  const [blob] = downloadBlob.mock.calls.at(-1);
  return await blob.text();
}

describe('exportShotsAsCsv', () => {
  beforeEach(() => {
    downloadBlob.mockClear();
  });

  test('empty filtered history produces a header-only CSV', async () => {
    exportShotsAsCsv([], 'gaggimate-shots-2026-01-01.csv');

    expect(downloadBlob).toHaveBeenCalledTimes(1);
    const [blob, filename] = downloadBlob.mock.calls[0];
    expect(filename).toBe('gaggimate-shots-2026-01-01.csv');
    expect(blob.type).toBe('text/csv;charset=utf-8');

    const csv = await csvFromLastCall();
    expect(csv).toBe(
      'id,timestamp,profile,duration_s,dose_in_g,dose_out_g,ratio,peak_pressure_bar,avg_pressure_bar,peak_flow_ml_s,avg_temp_c,notes',
    );
  });

  test('serialises a shot with samples and computes rounded aggregates', async () => {
    const shot = {
      id: 1,
      profile: 'Espresso',
      timestamp: 1735689600, // 2025-01-01T00:00:00.000Z
      duration: 28,
      notes: { doseIn: '18', doseOut: '36', ratio: '1:2', notes: 'nice shot' },
      samples: [
        { cp: 8.5, ct: 92.1, fl: 2.1 },
        { cp: 9.25, ct: 93.9, fl: 2.5 },
      ],
    };

    exportShotsAsCsv([shot], 'gaggimate-shots-2026-01-01.csv');

    const csv = await csvFromLastCall();
    const [, row] = csv.split('\n');
    expect(row).toBe(
      '1,2025-01-01T00:00:00.000Z,Espresso,28,18,36,1:2,9.25,8.88,2.50,93.00,nice shot',
    );
  });

  test('leaves computed columns blank when a sample field is absent', async () => {
    const shot = {
      id: 2,
      profile: 'No Pressure Data',
      timestamp: 1735689600,
      duration: 10,
      notes: {},
      samples: [{ ct: 90 }, { ct: 91 }],
    };

    exportShotsAsCsv([shot], 'gaggimate-shots-2026-01-01.csv');

    const csv = await csvFromLastCall();
    const [, row] = csv.split('\n');
    expect(row).toBe('2,2025-01-01T00:00:00.000Z,No Pressure Data,10,,,,,,,90.50,');
  });

  test('escapes commas, quotes, and newlines in the notes field', async () => {
    const shot = {
      id: 3,
      profile: 'Quoted, Profile',
      timestamp: 1735689600,
      duration: 5,
      notes: { notes: 'line1\nline2 with "quotes", and a comma' },
      samples: [],
    };

    exportShotsAsCsv([shot], 'gaggimate-shots-2026-01-01.csv');

    const csv = await csvFromLastCall();
    const expectedRow =
      '3,2025-01-01T00:00:00.000Z,"Quoted, Profile",5,,,,,,,,"line1\nline2 with ""quotes"", and a comma"';
    expect(csv).toBe(
      `id,timestamp,profile,duration_s,dose_in_g,dose_out_g,ratio,peak_pressure_bar,avg_pressure_bar,peak_flow_ml_s,avg_temp_c,notes\n${expectedRow}`,
    );
  });
});
