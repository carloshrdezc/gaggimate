import uuidv4 from '../../utils/uuid.js';
import { TclConverter } from './TclConverter.js';

const PROFILE_TYPES = new Set(['standard', 'pro']);
const PHASE_TYPES = new Set(['preinfusion', 'brew']);
const TRANSITION_TYPES = new Set(['instant', 'linear', 'ease-in', 'ease-out', 'ease-in-out']);
const TARGET_TYPES = new Set(['volumetric', 'pressure', 'flow', 'pumped']);
const PUMP_TARGETS = new Set(['pressure', 'flow']);

function isObject(value) {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function deepClone(value) {
  if (typeof structuredClone === 'function') return structuredClone(value);
  return JSON.parse(JSON.stringify(value));
}

function normalizeLabelKey(value) {
  return String(value || '')
    .trim()
    .toLowerCase()
    .replace(/\s+/g, ' ');
}

function formatProfileLabel(index) {
  return `Imported Profile ${index + 1}`;
}

function assertAllowedKeys(value, allowed, context) {
  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) {
      throw new Error(`${context} contains an unsupported property: ${key}`);
    }
  }
}

function normalizeTargets(rawTargets, context) {
  if (rawTargets == null) return [];
  if (!Array.isArray(rawTargets)) {
    throw new Error(`${context}.targets must be an array.`);
  }

  return rawTargets.map((target, index) => {
    if (!isObject(target)) {
      throw new Error(`${context}.targets[${index}] must be an object.`);
    }
    assertAllowedKeys(target, new Set(['type', 'operator', 'value']), `${context}.targets[${index}]`);

    const type = target.type;
    if (!TARGET_TYPES.has(type)) {
      throw new Error(`${context}.targets[${index}].type must be one of ${Array.from(TARGET_TYPES).join(', ')}.`);
    }

    const value = Number(target.value);
    if (!Number.isFinite(value) || value < 0) {
      throw new Error(`${context}.targets[${index}].value must be a non-negative number.`);
    }

    const normalized = { type, value };
    if (target.operator != null) {
      if (target.operator !== 'gte' && target.operator !== 'lte') {
        throw new Error(`${context}.targets[${index}].operator must be gte or lte.`);
      }
      normalized.operator = target.operator;
    }
    return normalized;
  });
}

function normalizePump(rawPump, context) {
  if (typeof rawPump === 'number') {
    if (rawPump !== 0 && rawPump !== 1) {
      throw new Error(`${context}.pump must be 0, 1, or a pump object.`);
    }
    return rawPump;
  }

  if (!isObject(rawPump)) {
    throw new Error(`${context}.pump must be 0, 1, or a pump object.`);
  }
  assertAllowedKeys(rawPump, new Set(['target', 'pressure', 'flow']), `${context}.pump`);

  if (!PUMP_TARGETS.has(rawPump.target)) {
    throw new Error(`${context}.pump.target must be pressure or flow.`);
  }

  const pressure = Number(rawPump.pressure);
  const flow = Number(rawPump.flow);
  if (!Number.isFinite(pressure) || pressure < -1) {
    throw new Error(`${context}.pump.pressure must be a number greater than or equal to -1.`);
  }
  if (!Number.isFinite(flow) || flow < -1) {
    throw new Error(`${context}.pump.flow must be a number greater than or equal to -1.`);
  }

  return {
    target: rawPump.target,
    pressure,
    flow,
  };
}

function normalizeTransition(rawTransition, context) {
  if (rawTransition == null) {
    return { type: 'instant', duration: 0, adaptive: false };
  }
  if (!isObject(rawTransition)) {
    throw new Error(`${context}.transition must be an object.`);
  }
  assertAllowedKeys(rawTransition, new Set(['type', 'duration', 'adaptive']), `${context}.transition`);

  if (!TRANSITION_TYPES.has(rawTransition.type)) {
    throw new Error(
      `${context}.transition.type must be one of ${Array.from(TRANSITION_TYPES).join(', ')}.`,
    );
  }

  const duration = Number(rawTransition.duration);
  if (!Number.isFinite(duration) || duration < 0) {
    throw new Error(`${context}.transition.duration must be a non-negative number.`);
  }

  const adaptive = rawTransition.adaptive;
  if (adaptive != null && typeof adaptive !== 'boolean' && adaptive !== 0 && adaptive !== 1) {
    throw new Error(`${context}.transition.adaptive must be boolean, 0, or 1.`);
  }

  return {
    type: rawTransition.type,
    duration,
    adaptive: adaptive == null ? false : Boolean(adaptive),
  };
}

function normalizePhase(rawPhase, index, profileContext) {
  if (!isObject(rawPhase)) {
    throw new Error(`${profileContext}.phases[${index}] must be an object.`);
  }
  assertAllowedKeys(
    rawPhase,
    new Set(['name', 'phase', 'valve', 'duration', 'transition', 'temperature', 'pump', 'targets']),
    `${profileContext}.phases[${index}]`,
  );

  const name = typeof rawPhase.name === 'string' && rawPhase.name.trim() ? rawPhase.name.trim() : `Phase ${index + 1}`;
  const phase = rawPhase.phase || 'brew';
  if (!PHASE_TYPES.has(phase)) {
    throw new Error(`${profileContext}.phases[${index}].phase must be preinfusion or brew.`);
  }

  const valve = rawPhase.valve == null ? 1 : Number(rawPhase.valve);
  if (!Number.isInteger(valve) || (valve !== 0 && valve !== 1)) {
    throw new Error(`${profileContext}.phases[${index}].valve must be 0 or 1.`);
  }

  const duration = Number(rawPhase.duration);
  if (!Number.isFinite(duration) || duration < 0) {
    throw new Error(`${profileContext}.phases[${index}].duration must be a non-negative number.`);
  }

  const phaseObject = {
    name,
    phase,
    valve,
    duration,
    pump: normalizePump(rawPhase.pump, `${profileContext}.phases[${index}]`),
  };

  if (rawPhase.temperature != null) {
    const temperature = Number(rawPhase.temperature);
    if (!Number.isFinite(temperature)) {
      throw new Error(`${profileContext}.phases[${index}].temperature must be a number.`);
    }
    phaseObject.temperature = temperature;
  }

  if (rawPhase.transition != null) {
    phaseObject.transition = normalizeTransition(rawPhase.transition, `${profileContext}.phases[${index}]`);
  } else {
    phaseObject.transition = { type: 'instant', duration: 0, adaptive: false };
  }

  if (rawPhase.targets != null) {
    const targets = normalizeTargets(rawPhase.targets, `${profileContext}.phases[${index}]`);
    if (targets.length > 0) {
      phaseObject.targets = targets;
    }
  }

  return phaseObject;
}

function normalizeImportedProfile(rawProfile, index, { keepId = false } = {}) {
  if (!isObject(rawProfile)) {
    throw new Error(`Imported profile ${index + 1} must be an object.`);
  }

  assertAllowedKeys(
    rawProfile,
    new Set(['id', 'label', 'type', 'description', 'temperature', 'favorite', 'selected', 'utility', 'phases', 'debug']),
    `Imported profile ${index + 1}`,
  );

  const label = typeof rawProfile.label === 'string' && rawProfile.label.trim() ? rawProfile.label.trim() : formatProfileLabel(index);
  const type = rawProfile.type && PROFILE_TYPES.has(rawProfile.type) ? rawProfile.type : 'pro';
  const description = typeof rawProfile.description === 'string' ? rawProfile.description : '';
  const temperature = rawProfile.temperature == null ? 93 : Number(rawProfile.temperature);
  if (!Number.isFinite(temperature)) {
    throw new Error(`Imported profile ${index + 1} must have a numeric temperature.`);
  }

  if (!Array.isArray(rawProfile.phases) || rawProfile.phases.length === 0) {
    throw new Error(`Imported profile ${index + 1} must include at least one phase.`);
  }

  const normalized = {
    label,
    type,
    description,
    temperature,
    favorite: Boolean(rawProfile.favorite),
    selected: Boolean(rawProfile.selected),
    utility: Boolean(rawProfile.utility),
    phases: rawProfile.phases.map((phase, phaseIndex) => normalizePhase(phase, phaseIndex, `Imported profile ${index + 1}`)),
  };

  if (keepId && typeof rawProfile.id === 'string' && rawProfile.id.trim()) {
    normalized.id = rawProfile.id.trim();
  }

  if (typeof rawProfile.debug !== 'undefined') {
    normalized.debug = deepClone(rawProfile.debug);
  }

  return normalized;
}

function normalizeImportedPayload(payload) {
  if (Array.isArray(payload)) {
    return payload.map((item, index) => normalizeImportedProfile(item, index));
  }

  if (isObject(payload) && Array.isArray(payload.profiles)) {
    return payload.profiles.map((item, index) => normalizeImportedProfile(item, index));
  }

  if (isObject(payload) && Array.isArray(payload.phases)) {
    return [normalizeImportedProfile(payload, 0)];
  }

  throw new Error('JSON must contain a profile object, a profiles array, or a bundle with profiles.');
}

export function serializeProfileForExport(profile) {
  if (!isObject(profile)) {
    throw new Error('Cannot export an invalid profile.');
  }

  const copy = deepClone(profile);
  delete copy.id;
  delete copy.favorite;
  delete copy.selected;
  return copy;
}

export function serializeProfilesForExport(profiles) {
  if (!Array.isArray(profiles)) {
    throw new Error('Cannot export profiles: expected an array.');
  }

  return profiles.map(profile => serializeProfileForExport(profile));
}

export function parseProfileImportText(text) {
  if (typeof text !== 'string' || !text.trim()) {
    throw new Error('Import file is empty.');
  }

  const cleanText = text.replace(/^\uFEFF/, '').trim();
  if (cleanText.startsWith('{') || cleanText.startsWith('[')) {
    try {
      const payload = JSON.parse(cleanText);
      return {
        source: 'json',
        profiles: normalizeImportedPayload(payload),
      };
    } catch (error) {
      throw new Error(`Invalid JSON: ${error.message}`);
    }
  }

  const result = TclConverter.toGaggiMate(cleanText);
  if (!result.ok) {
    throw new Error(`Unsupported profile format: ${result.reason}`);
  }

  return {
    source: 'tcl',
    profiles: [normalizeImportedProfile(result.json, 0)],
  };
}

export function getProfileImportConflicts(importedProfiles, existingProfiles) {
  const existingByLabel = new Map();
  for (const profile of existingProfiles || []) {
    if (profile?.label) {
      existingByLabel.set(normalizeLabelKey(profile.label), profile);
    }
  }

  const conflicts = [];
  for (const profile of importedProfiles || []) {
    const conflict = existingByLabel.get(normalizeLabelKey(profile.label));
    if (conflict) {
      conflicts.push({ imported: profile, existing: conflict });
    }
  }
  return conflicts;
}

function makeUniqueLabel(label, usedLabels) {
  const base = String(label || '').trim() || 'Imported Profile';
  let candidate = base;
  let suffix = 1;

  while (usedLabels.has(normalizeLabelKey(candidate))) {
    candidate = suffix === 1 ? `${base} (Imported)` : `${base} (Imported ${suffix})`;
    suffix += 1;
  }

  usedLabels.add(normalizeLabelKey(candidate));
  return candidate;
}

export function planProfileImports(importedProfiles, existingProfiles, options = {}) {
  const mode = options.mode === 'overwrite' ? 'overwrite' : 'rename';
  const existingByLabel = new Map();
  const usedLabels = new Set();

  for (const profile of existingProfiles || []) {
    if (profile?.label) {
      const key = normalizeLabelKey(profile.label);
      existingByLabel.set(key, profile);
      usedLabels.add(key);
    }
  }

  return (importedProfiles || []).map((profile, index) => {
    const existing = existingByLabel.get(normalizeLabelKey(profile.label));
    const candidate = deepClone(profile);

    if (mode === 'overwrite' && existing) {
      candidate.id = existing.id;
      candidate.favorite = Boolean(existing.favorite);
      candidate.selected = Boolean(existing.selected);
      candidate.label = existing.label;
      return normalizeImportedProfile(candidate, index, { keepId: true });
    }

    candidate.id = uuidv4();
    candidate.label = makeUniqueLabel(candidate.label, usedLabels);
    candidate.favorite = false;
    candidate.selected = false;
    const planned = normalizeImportedProfile(candidate, index, { keepId: true });
    usedLabels.add(normalizeLabelKey(planned.label));
    return planned;
  });
}
