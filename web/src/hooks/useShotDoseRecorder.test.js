import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { renderHook, waitFor, cleanup, act } from '@testing-library/preact';

// Mock the NotesService singleton the hook writes through.
const saveNotes = vi.fn().mockResolvedValue(undefined);
const saveNotesIfMissing = vi.fn().mockResolvedValue(undefined);
const setApiService = vi.fn();
vi.mock('../pages/ShotAnalyzer/services/NotesService', () => ({
  notesService: {
    setApiService: (...args) => setApiService(...args),
    saveNotes: (...args) => saveNotes(...args),
    saveNotesIfMissing: (...args) => saveNotesIfMissing(...args),
  },
}));

import { machine } from '../services/ApiService.js';
import { useShotDoseRecorder } from './useShotDoseRecorder.js';

const api = { id: 'api' };

function setStatus({ shotId = null, active = true, selectedBean = '' } = {}) {
  act(() => {
    machine.value = {
      ...machine.value,
      status: {
        ...machine.value.status,
        selectedBean,
        process: shotId ? { id: shotId, a: active } : null,
      },
    };
  });
}

beforeEach(() => {
  saveNotes.mockClear().mockResolvedValue(undefined);
  saveNotesIfMissing.mockClear().mockResolvedValue(undefined);
  setApiService.mockClear();
  setStatus({ shotId: null });
});

afterEach(() => {
  cleanup();
});

describe('useShotDoseRecorder — grind auto-attach', () => {
  it('attaches the manual grind value once per shot start via fill-only-if-absent', async () => {
    renderHook(() => useShotDoseRecorder(api, 18, 14.5));

    setStatus({ shotId: 'shot-1', active: true });

    await waitFor(() => {
      expect(saveNotesIfMissing).toHaveBeenCalledWith('shot-1', 'gaggimate', {
        grindSetting: '14.5',
      });
    });
    // dose/bean still attach via the overwrite path
    expect(saveNotes).toHaveBeenCalledWith('shot-1', 'gaggimate', { doseIn: 18 });

    // Subsequent status frames for the same shot must not re-attach.
    setStatus({ shotId: 'shot-1', active: true });
    setStatus({ shotId: 'shot-1', active: true });
    await Promise.resolve();
    expect(saveNotesIfMissing).toHaveBeenCalledTimes(1);
  });

  it('does not attach grind when manualGrind is 0 (the not-set sentinel)', async () => {
    renderHook(() => useShotDoseRecorder(api, 18, 0));

    setStatus({ shotId: 'shot-1', active: true });

    await waitFor(() => {
      expect(saveNotes).toHaveBeenCalledWith('shot-1', 'gaggimate', { doseIn: 18 });
    });
    expect(saveNotesIfMissing).not.toHaveBeenCalled();
  });

  it('attaches grind for a new shot after the previous shot ends', async () => {
    renderHook(() => useShotDoseRecorder(api, 18, 14.5));

    setStatus({ shotId: 'shot-1', active: true });
    await waitFor(() => expect(saveNotesIfMissing).toHaveBeenCalledTimes(1));

    setStatus({ shotId: null }); // shot ends -> ref resets
    setStatus({ shotId: 'shot-2', active: true });

    await waitFor(() => {
      expect(saveNotesIfMissing).toHaveBeenCalledWith('shot-2', 'gaggimate', {
        grindSetting: '14.5',
      });
    });
    expect(saveNotesIfMissing).toHaveBeenCalledTimes(2);
  });
});

describe('useShotDoseRecorder — failed save retry', () => {
  it('does not permanently block a later successful attach for the same shot ID', async () => {
    // First attempt fails.
    saveNotes.mockRejectedValueOnce(new Error('ws hiccup'));
    const errSpy = vi.spyOn(console, 'error').mockImplementation(() => {});

    renderHook(() => useShotDoseRecorder(api, 18, 0));

    // Shot appears -> first attach attempt runs and its save rejects.
    setStatus({ shotId: 'shot-1', active: true });
    await waitFor(() => expect(errSpy).toHaveBeenCalled());
    // Let the failed attempt fully settle (in-flight guard cleared, ref left null).
    await act(async () => {
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(saveNotes).toHaveBeenCalledTimes(1);

    // Because the save failed, the shot must NOT be marked saved: a subsequent
    // status tick for the same shot should retry, and this time succeed.
    setStatus({ shotId: 'shot-1', active: true });

    await waitFor(() => expect(saveNotes).toHaveBeenCalledTimes(2));
    // Both calls target the same shot ID.
    expect(saveNotes.mock.calls[0][0]).toBe('shot-1');
    expect(saveNotes.mock.calls[1][0]).toBe('shot-1');

    // After the successful retry, further ticks do not re-attach.
    setStatus({ shotId: 'shot-1', active: true });
    await Promise.resolve();
    expect(saveNotes).toHaveBeenCalledTimes(2);

    errSpy.mockRestore();
  });
});
