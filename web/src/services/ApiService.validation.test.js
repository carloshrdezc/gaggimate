import { describe, expect, test } from 'vitest';
import { validateWebSocketRequest } from './ApiService.js';

describe('WebSocket request validation (PRO-521)', () => {
  test('rejects malformed, non-finite, and out-of-range command values before send', () => {
    expect(() => validateWebSocketRequest({ tp: 'req:dose:set', grams: '10' })).toThrow('grams');
    expect(() => validateWebSocketRequest({ tp: 'req:dose:set', grams: Infinity })).toThrow('grams');
    expect(() => validateWebSocketRequest({ tp: 'req:change-mode', mode: 9 })).toThrow('mode');
    expect(() => validateWebSocketRequest({ tp: 'req:manual:update', targetType: 'wat' })).toThrow('targetType');
  });

  test('preserves valid requests and ignores unknown fields for forward compatibility', () => {
    expect(validateWebSocketRequest({ tp: 'req:dose:set', grams: 18.5, futureField: 'ignored' })).toEqual({
      tp: 'req:dose:set',
      grams: 18.5,
      futureField: 'ignored',
    });
  });
});
