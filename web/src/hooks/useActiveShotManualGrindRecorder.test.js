import { describe, expect, it, vi } from 'vitest';
import { persistManualGrindForActiveShot } from './useActiveShotManualGrindRecorder.js';

function status({ shotId = 'shot-1', active = true, requestId = 'request-1' } = {}) {
  return { process: shotId ? { id: shotId, a: active, rid: requestId } : null };
}

function makeNotesService() {
  return {
    setApiService: vi.fn(),
    saveNotesIfMissing: vi.fn().mockResolvedValue(undefined),
  };
}

describe('persistManualGrindForActiveShot', () => {
  it('saves a committed manual setting for the active recording shot', async () => {
    const notesService = makeNotesService();
    const getStatus = vi.fn(() => status());

    await persistManualGrindForActiveShot({
      api: { id: 'api' },
      grindSetting: '3.2',
      getStatus,
      notesService,
    });

    expect(notesService.saveNotesIfMissing).toHaveBeenCalledWith(
      'shot-1',
      'gaggimate',
      { grindSetting: '3.2' },
      expect.any(Function),
    );
  });

  it('does not save when no recording shot is active', async () => {
    const notesService = makeNotesService();

    await persistManualGrindForActiveShot({
      api: {},
      grindSetting: '3.2',
      getStatus: () => status({ shotId: null }),
      notesService,
    });

    expect(notesService.saveNotesIfMissing).not.toHaveBeenCalled();
  });

  it('marks the conditional device write stale when the active shot changes while it is pending', async () => {
    const notesService = makeNotesService();
    let currentStatus = status();
    notesService.saveNotesIfMissing.mockImplementation(async (_id, _source, _notes, shouldSave) => {
      currentStatus = status({ shotId: 'shot-2', requestId: 'request-2' });
      expect(shouldSave()).toBe(false);
    });

    await persistManualGrindForActiveShot({
      api: {},
      grindSetting: '3.2',
      getStatus: () => currentStatus,
      notesService,
    });

    expect(notesService.saveNotesIfMissing).toHaveBeenCalledTimes(1);
  });

  it('deduplicates concurrent commits for the same active-shot request', async () => {
    let resolveSave;
    const notesService = makeNotesService();
    notesService.saveNotesIfMissing.mockReturnValue(
      new Promise(resolve => {
        resolveSave = resolve;
      }),
    );
    const options = {
      api: {},
      grindSetting: '3.2',
      getStatus: () => status(),
      notesService,
    };

    const first = persistManualGrindForActiveShot(options);
    const second = persistManualGrindForActiveShot(options);
    resolveSave();
    await Promise.all([first, second]);

    expect(notesService.saveNotesIfMissing).toHaveBeenCalledTimes(1);
  });

  it('invalidates an earlier manual edit before it can save', async () => {
    const notesService = makeNotesService();

    await persistManualGrindForActiveShot({
      api: {},
      grindSetting: '3.2',
      getStatus: () => status(),
      isRequestCurrent: () => false,
      notesService,
    });

    expect(notesService.saveNotesIfMissing).not.toHaveBeenCalled();
  });
});
