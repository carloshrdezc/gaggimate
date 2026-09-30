import { describe, expect, it } from 'vitest';
import { PROCESS_REFUSAL_TYPES, processRefusalMessage } from './useProcessActions.js';

describe('processRefusalMessage (PRO-655 B-P2-2)', () => {
  it('returns the firmware error for a refused activate', () => {
    expect(
      processRefusalMessage({ tp: 'res:process:activate', success: false, error: 'Controller not ready: x' }),
    ).toBe('Controller not ready: x');
    expect(processRefusalMessage({ tp: 'res:grind:activate', success: false })).toBe('Controller not ready');
  });

  it('ignores non-refusals and unrelated messages', () => {
    expect(processRefusalMessage({ tp: 'res:process:activate', success: true })).toBeNull();
    expect(processRefusalMessage({ tp: 'evt:status' })).toBeNull();
    expect(processRefusalMessage(null)).toBeNull();
  });

  it('covers both activate replies', () => {
    expect(PROCESS_REFUSAL_TYPES).toEqual(['res:process:activate', 'res:grind:activate']);
  });
});
