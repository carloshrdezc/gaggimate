import { TclConverter } from './TclConverter.js';

// A parsed value only counts as a profile if it carries the universal profile
// marker: a `phases` array. PRO-218 (P2 parse-leniency): a valid-JSON document
// that is NOT a profile ({}, a bare primitive, an unrelated JSON object) used to
// be wrapped as `[value]` and counted as one "ok" profile, so it escaped the
// file-level corrupt detection (`ok = count > 0`) and was fed to the restore as a
// bogus entry. Both the standard and pro profile schemas, and every TCL/JSON
// conversion path, produce a `phases` array — so this is a safe discriminator
// that turns "valid JSON, wrong shape" into a failed/empty parse instead of a
// silent bogus profile.
function isProfileShaped(value) {
  return value != null && typeof value === 'object' && Array.isArray(value.phases);
}

export function parseProfile(input) {
  try {
    let profiles = JSON.parse(input);
    if (!Array.isArray(profiles)) {
      profiles = [parseJsonProfile(profiles)];
    }
    // Drop anything that isn't profile-shaped: a valid-JSON-but-non-profile file
    // must count as an empty/failed parse, not a silent bogus profile (PRO-218).
    const shaped = profiles.filter(isProfileShaped);
    return shaped;
  } catch (ignored) {
    const result = TclConverter.toGaggiMate(input);
    if (result.ok) {
      return [result.json];
    }
    // Input isn't JSON, try TCL
  }
  return [];
}

function parseJsonProfile(input) {
  if (input.waterTemperature) {
    let profile = {
      label: input.name,
      type: 'pro',
      temperature: input.waterTemperature,
      phases: [],
    };

    let isPositive = function (v) {
      return typeof v === 'number' && v > 0 && Number.isFinite(v);
    };

    for (let i = 0; i < input.phases.length; i++) {
      let p = input.phases[i];
      let phase = {
        name: p && typeof p.name === 'string' && p.name.trim() ? p.name : `Phase ${i + 1}`,
        valve: 1,
        pump: 0,
        duration: Math.max(p.target.time, p.stopConditions.time) / 1000,
        targets: [],
        temperature: isPositive(p.waterTemperature) ? p.waterTemperature : 0,
        transition: {
          type: p.target.curve.toLowerCase().replace('_', '-'),
          duration: p.target.time / 1000,
          adaptive: true,
        },
      };
      if (p.target.end > 0) {
        if (p.type == 'PRESSURE') {
          phase.pump = {
            target: 'pressure',
            pressure: p.target.end,
            flow: p.restriction,
          };
        } else {
          phase.pump = {
            target: 'flow',
            pressure: p.restriction,
            flow: p.target.end,
          };
        }
      }

      const conditions = p.stopConditions || {};
      if (isPositive(conditions.pressureAbove)) {
        phase.targets.push({ type: 'pressure', value: conditions.pressureAbove });
      }
      if (isPositive(conditions.pressureBelow)) {
        phase.targets.push({ type: 'pressure', operator: 'lte', value: conditions.pressureBelow });
      }
      if (isPositive(conditions.flowAbove)) {
        phase.targets.push({ type: 'flow', value: conditions.flowAbove });
      }
      if (isPositive(conditions.flowBelow)) {
        phase.targets.push({ type: 'flow', operator: 'lte', value: conditions.flowBelow });
      }
      if (isPositive(conditions.weight)) {
        phase.targets.push({ type: 'volumetric', value: conditions.weight });
      } else if (isPositive(input.globalStopConditions?.weight)) {
        phase.targets.push({ type: 'volumetric', value: input.globalStopConditions.weight });
      }
      if (isPositive(conditions.waterPumpedInPhase)) {
        phase.targets.push({ type: 'pumped', value: conditions.waterPumpedInPhase });
      }

      profile.phases.push(phase);
    }

    return profile;
  }
  return input;
}
