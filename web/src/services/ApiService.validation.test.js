import { describe, expect, test } from 'vitest';
import { validateWebSocketRequest } from './ApiService.js';

describe('WebSocket request validation (PRO-521)', () => {
  test('rejects malformed, non-finite, and out-of-range command values before send', () => {
    expect(() => validateWebSocketRequest({ tp: 'req:dose:set', grams: '10' })).toThrow('grams');
    expect(() => validateWebSocketRequest({ tp: 'req:dose:set', grams: Infinity })).toThrow('grams');
    expect(() => validateWebSocketRequest({ tp: 'req:manual-grind:set', value: '10' })).toThrow('value');
    expect(() => validateWebSocketRequest({ tp: 'req:manual-grind:set', value: Infinity })).toThrow('value');
    expect(() => validateWebSocketRequest({ tp: 'req:manual-grind:set', value: 150 })).toThrow('value');
    expect(() => validateWebSocketRequest({ tp: 'req:manual-grind:set', value: -1 })).toThrow('value');
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

  test('accepts a valid manual-grind:set including the 0 "not set" sentinel (PRO-603)', () => {
    // 0 is a valid grind value (dial "not set"), unlike dose which rejects <= 0.
    expect(validateWebSocketRequest({ tp: 'req:manual-grind:set', value: 0 })).toEqual({
      tp: 'req:manual-grind:set',
      value: 0,
    });
    expect(validateWebSocketRequest({ tp: 'req:manual-grind:set', value: 100 })).toEqual({
      tp: 'req:manual-grind:set',
      value: 100,
    });
    expect(validateWebSocketRequest({ tp: 'req:manual-grind:set', value: 42.5 })).toEqual({
      tp: 'req:manual-grind:set',
      value: 42.5,
    });
  });

  test('preserves the PRO-587 auto flag on an automatic standby-on-brew change-mode', () => {
    // PRO-587: the standby-on-brew effect sends `req:change-mode` mode=STANDBY
    // with `auto: true` so the firmware defers the stop through the settle
    // window (like Auto-Steam). The validator must accept and preserve the flag;
    // an explicit STANDBY sends no `auto` field and keeps instant-stop behavior.
    expect(validateWebSocketRequest({ tp: 'req:change-mode', mode: 0, auto: true })).toEqual({
      tp: 'req:change-mode',
      mode: 0,
      auto: true,
    });
  });
});
