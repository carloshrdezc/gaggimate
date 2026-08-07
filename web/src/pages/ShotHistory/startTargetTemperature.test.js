import { describe, test, expect } from 'vitest';
import {
  deriveStartTargetTemperature,
  formatStartTargetTemperature,
} from './startTargetTemperature.js';

// PRO-631: the derived value must come from the shot's own log samples and must
// degrade to N/A rather than fabricate a target.
//
// Every profile-based fixture carries a `profileId`, because that is what the
// firmware writes into the shot header for a profile shot (initializeHeader in
// ShotHistoryPlugin.cpp) and what parseBinaryShot/parseBinaryIndex decode onto
// the parsed shot. A shot with no profile identity is a manual shot.
const PROFILE_ID = 'profile-1';

function profileShot(overrides = {}) {
  return { profileId: PROFILE_ID, ...overrides };
}

describe('deriveStartTargetTemperature', () => {
  test('returns the first valid tt sample (valid target)', () => {
    const shot = profileShot({
      samples: [
        { t: 0, tt: 93.5, ct: 88.1 },
        { t: 250, tt: 93.5, ct: 90.2 },
      ],
    });
    expect(deriveStartTargetTemperature(shot)).toBe(93.5);
  });

  test('skips leading zero/NaN samples and uses the first plausible one', () => {
    const shot = profileShot({
      samples: [{ tt: 0 }, { tt: Number.NaN }, { tt: 92.3 }, { tt: 96 }],
    });
    expect(deriveStartTargetTemperature(shot)).toBe(92.3);
  });

  test('rounds to 0.1 °C resolution', () => {
    // tt arrives as a uint16 / TEMP_SCALE, so 935/10 can carry float dust.
    const shot = profileShot({ samples: [{ tt: 93.44999999999999 }] });
    expect(deriveStartTargetTemperature(shot)).toBe(93.4);
  });

  test('phase-changing trace still reports only the START target', () => {
    const shot = profileShot({
      samples: [
        { tt: 92, phaseNumber: 0, phaseName: 'Preinfusion' },
        { tt: 92, phaseNumber: 0, phaseName: 'Preinfusion' },
        { tt: 96.5, phaseNumber: 1, phaseName: 'Ramp' },
        { tt: 88, phaseNumber: 2, phaseName: 'Decline' },
      ],
    });
    expect(deriveStartTargetTemperature(shot)).toBe(92);
  });

  test('absent target: every sample has tt = 0', () => {
    expect(
      deriveStartTargetTemperature(profileShot({ samples: [{ tt: 0 }, { tt: 0 }] })),
    ).toBeNull();
  });

  describe('manual shots', () => {
    // The firmware's createSample() stamps sample.tt from the live controller
    // target for EVERY sample regardless of mode, so a manual shot pulled at a
    // real setpoint DOES carry plausible non-zero tt samples. Only the header
    // marks it manual (profileId = "manual"), so the guard must key off that —
    // otherwise a manual shot fabricates a "requested" brew target it never had.
    test('manual shot with plausible non-zero tt samples resolves to null', () => {
      const manualShot = {
        profileId: 'manual',
        profile: 'Manual',
        samples: [
          { t: 0, tt: 93.5, cp: 6.2, ct: 92.1 },
          { t: 250, tt: 93.5, cp: 8.8, ct: 92.4 },
        ],
      };
      expect(deriveStartTargetTemperature(manualShot)).toBeNull();
      expect(formatStartTargetTemperature(deriveStartTargetTemperature(manualShot))).toBe('N/A');
    });

    test('manual shot whose samples carry no target field at all is also null', () => {
      const manualShot = {
        profileId: 'manual',
        profile: 'Manual',
        samples: [
          { t: 0, cp: 6.2, ct: 92.1 },
          { t: 250, cp: 8.8, ct: 92.4 },
        ],
      };
      expect(deriveStartTargetTemperature(manualShot)).toBeNull();
    });

    test('shot with no profile identity (missing/blank profileId) is treated as manual', () => {
      // Same convention as HistoryCard.jsx's isManualShot.
      expect(deriveStartTargetTemperature({ samples: [{ tt: 93.5 }] })).toBeNull();
      expect(deriveStartTargetTemperature({ profileId: '', samples: [{ tt: 93.5 }] })).toBeNull();
      expect(
        deriveStartTargetTemperature({ profileId: '   ', samples: [{ tt: 93.5 }] }),
      ).toBeNull();
    });
  });

  test('old logs missing the TT field bit entirely degrade to null', () => {
    // fieldsMask without the TT bit → parseBinaryShot never sets sample.tt
    const legacyShot = profileShot({
      version: 4,
      fieldsMask: 0b101, // T + CT only
      samples: [
        { t: 0, ct: 91.5 },
        { t: 250, ct: 92.0 },
      ],
    });
    expect(deriveStartTargetTemperature(legacyShot)).toBeNull();
  });

  test('malformed values (non-numeric, negative, absurdly high) are rejected', () => {
    expect(deriveStartTargetTemperature(profileShot({ samples: [{ tt: '93' }] }))).toBeNull();
    expect(deriveStartTargetTemperature(profileShot({ samples: [{ tt: -5 }] }))).toBeNull();
    expect(deriveStartTargetTemperature(profileShot({ samples: [{ tt: 6553.5 }] }))).toBeNull();
    expect(deriveStartTargetTemperature(profileShot({ samples: [{ tt: Infinity }] }))).toBeNull();
    expect(
      deriveStartTargetTemperature(profileShot({ samples: [null, undefined, { tt: 94 }] })),
    ).toBe(94);
  });

  test('missing/empty samples and missing shot degrade to null', () => {
    expect(deriveStartTargetTemperature(profileShot({ samples: [] }))).toBeNull();
    expect(deriveStartTargetTemperature(profileShot())).toBeNull();
    expect(deriveStartTargetTemperature({})).toBeNull();
    expect(deriveStartTargetTemperature(null)).toBeNull();
    expect(deriveStartTargetTemperature(undefined)).toBeNull();
    // Backup entries that hold notes but no sample trace
    expect(deriveStartTargetTemperature(profileShot({ samples: null, loaded: false }))).toBeNull();
  });
});

describe('formatStartTargetTemperature', () => {
  test('formats a derived target with its unit at 0.1 °C resolution', () => {
    expect(formatStartTargetTemperature(93.5)).toBe('93.5 °C');
    expect(formatStartTargetTemperature(92)).toBe('92.0 °C');
  });

  test('degrades to N/A for null/absent/invalid values', () => {
    expect(formatStartTargetTemperature(null)).toBe('N/A');
    expect(formatStartTargetTemperature(undefined)).toBe('N/A');
    expect(formatStartTargetTemperature(0)).toBe('N/A');
    expect(formatStartTargetTemperature(Number.NaN)).toBe('N/A');
  });
});
