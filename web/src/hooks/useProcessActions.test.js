import { describe, expect, it } from 'vitest';
import {
  PROCESS_REFUSAL_TYPES,
  processRefusalMessage,
  refusalPrefix,
  useProcessRefusalNotice,
} from './useProcessActions.js';

describe('processRefusalMessage (PRO-655 B-P2-2)', () => {
  it('returns the firmware error for a refused activate', () => {
    expect(
      processRefusalMessage({
        tp: 'res:process:activate',
        success: false,
        error: 'Controller not ready: x',
      }),
    ).toBe('Controller not ready: x');
    expect(processRefusalMessage({ tp: 'res:grind:activate', success: false })).toBe(
      'Controller not ready',
    );
  });

  it('ignores non-refusals and unrelated messages', () => {
    expect(processRefusalMessage({ tp: 'res:process:activate', success: true })).toBeNull();
    expect(processRefusalMessage({ tp: 'evt:status' })).toBeNull();
    expect(processRefusalMessage(null)).toBeNull();
  });

  it('covers both activate replies and the change-mode refusal', () => {
    expect(PROCESS_REFUSAL_TYPES).toEqual([
      'res:process:activate',
      'res:grind:activate',
      'res:change-mode',
    ]);
  });
});

describe('change-mode refusal (PRO-670)', () => {
  it('extracts the error from a refused change-mode', () => {
    expect(
      processRefusalMessage({
        tp: 'res:change-mode',
        success: false,
        error: 'Controller not ready: y',
      }),
    ).toBe('Controller not ready: y');
    expect(processRefusalMessage({ tp: 'res:change-mode', success: true })).toBeNull();
  });

  it('uses a leave-standby prefix for change-mode and keeps the start prefix otherwise', () => {
    expect(refusalPrefix('res:change-mode')).toBe('Cannot leave standby');
    expect(refusalPrefix('res:process:activate')).toBe('Cannot start');
    expect(refusalPrefix('res:grind:activate')).toBe('Cannot start');
  });
});

describe('useProcessRefusalNotice wiring (PRO-670)', () => {
  it('subscribes to res:change-mode and notifies with the leave-standby text', async () => {
    const { h, render } = await import('preact');
    const handlers = {};
    const api = {
      on: (tp, cb) => {
        handlers[tp] = cb;
        return tp;
      },
      off: () => {},
    };
    const notes = [];
    const notify = m => notes.push(m);
    function Probe() {
      useProcessRefusalNotice(api, notify);
      return null;
    }
    const root = document.createElement('div');
    const { act } = await import('preact/test-utils');
    act(() => render(h(Probe, null), root));
    expect(Object.keys(handlers)).toContain('res:change-mode');
    handlers['res:change-mode']({
      tp: 'res:change-mode',
      success: false,
      error: 'Controller not ready',
    });
    expect(notes).toEqual(['Cannot leave standby: Controller not ready']);
    act(() => render(null, root));
  });
});
