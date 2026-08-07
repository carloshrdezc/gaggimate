import { describe, test, expect, vi, beforeEach } from 'vitest';

vi.mock('../download.js', () => ({
  downloadBlob: vi.fn(),
}));

import { downloadBlob } from '../download.js';
import { downloadBeanconquerorBackup } from './beanconquerorDownload.js';

const BEAN = { id: 'b1', name: 'Pink Bourbon', quantity: 250, createdAt: 1_754_000_000 };
const SHOT = {
  id: '1',
  source: 'gaggimate',
  timestamp: 1_754_000_500,
  duration: 27_000,
  volume: 36,
  notes: { beanId: 'b1', doseIn: '18', doseOut: '36', grinder: 'Niche Zero' },
};

describe('downloadBeanconquerorBackup', () => {
  beforeEach(() => {
    downloadBlob.mockClear();
  });

  test('downloads a Beanconqueror.zip blob and reports the record counts', async () => {
    const result = await downloadBeanconquerorBackup({ beans: [BEAN], shots: [SHOT] });

    expect(result).toEqual({ filename: 'Beanconqueror.zip', beanCount: 1, brewCount: 1 });
    expect(downloadBlob).toHaveBeenCalledTimes(1);

    const [blob, filename] = downloadBlob.mock.calls[0];
    expect(filename).toBe('Beanconqueror.zip');
    expect(blob.type).toBe('application/zip');
    expect(blob.size).toBeGreaterThan(0);
  });

  test('forwards the deferred-download target window to downloadBlob', async () => {
    const targetWindow = { closed: false };
    await downloadBeanconquerorBackup({ beans: [BEAN], shots: [SHOT] }, { targetWindow });

    expect(downloadBlob.mock.calls[0][2]).toEqual({ targetWindow });
  });

  test('exports an empty library without downloading a broken archive', async () => {
    const result = await downloadBeanconquerorBackup({ beans: [], shots: [] });

    expect(result.brewCount).toBe(0);
    expect(downloadBlob).toHaveBeenCalledTimes(1);
  });
});
