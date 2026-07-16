const DEFAULT_HYDRATION_LIMIT = 10;
const DEFAULT_HYDRATION_CONCURRENCY = 3;

export function getShotNotesKey(shot) {
  return shot.source === 'browser'
    ? String(shot.storageKey || shot.name || shot.id || '')
    : String(shot.id);
}

function applyNotesRating(shot, notes) {
  if (!notes) return shot;

  const rating = notes.rating ?? shot.rating ?? 0;
  if (shot.notes === notes && shot.rating === rating) return shot;

  return {
    ...shot,
    notes,
    rating,
  };
}

const NOTES_STORE_CHECKED = '__shotHistoryNotesStoreChecked';
const PERSISTED_NOTES_FOUND = '__shotHistoryNotesStoreFound';
const NOTES_SERVICE_PERSISTED = '__shotHistoryPersistedNotes';

function markNotesStoreChecked(notes, persisted = true) {
  if (!notes || typeof notes !== 'object') return notes;

  try {
    Object.defineProperty(notes, NOTES_STORE_CHECKED, {
      value: true,
      enumerable: false,
      configurable: true,
    });
    if (persisted) {
      Object.defineProperty(notes, PERSISTED_NOTES_FOUND, {
        value: true,
        enumerable: false,
        configurable: true,
      });
    }
  } catch {
    // Frozen notes objects can still be used for this hydration run; they just
    // won't carry the non-enumerable repeat-load optimization marker.
  }

  return notes;
}

function hasPersistedNotes(notes, service) {
  if (!notes || typeof notes !== 'object') return false;

  if (notes[NOTES_SERVICE_PERSISTED]) return true;

  const defaults = service?.getDefaults?.(notes.id);
  return Object.entries(notes).some(([key, value]) => {
    if (
      key === 'id' ||
      key === NOTES_STORE_CHECKED ||
      key === PERSISTED_NOTES_FOUND ||
      key === NOTES_SERVICE_PERSISTED
    ) {
      return false;
    }
    if (defaults && Object.is(value, defaults[key])) return false;
    if (key === 'balanceTaste' && value === 'balanced') return false;
    if (typeof value === 'number') return value > 0;
    return value != null && value !== '';
  });
}

function chooseLoadedNotes(shot, notes, service) {
  const persisted = hasPersistedNotes(notes, service);
  if (shot?.source === 'browser' && shot.notes && !persisted) {
    return markNotesStoreChecked(shot.notes, false);
  }

  return markNotesStoreChecked(notes, persisted);
}

function shouldLoadPersistedNotes(shot) {
  return (
    !shot.notes ||
    (shot.source === 'browser' &&
      !shot.notes[PERSISTED_NOTES_FOUND] &&
      !shot.notes[NOTES_STORE_CHECKED])
  );
}

export async function hydrateShotRatingsFromNotes(
  shots,
  service,
  { limit = DEFAULT_HYDRATION_LIMIT, concurrency = DEFAULT_HYDRATION_CONCURRENCY } = {},
) {
  const visibleShots = Array.isArray(shots) ? shots.slice(0, limit) : [];
  const hydrated = new Map();
  const pending = visibleShots.filter(shot => {
    if (!shot) return false;
    if (!shouldLoadPersistedNotes(shot)) {
      hydrated.set(`${shot.source || 'gaggimate'}:${shot.id}`, applyNotesRating(shot, shot.notes));
      return false;
    }
    return true;
  });
  let cursor = 0;

  const workerCount = Math.max(0, Math.min(concurrency, pending.length));
  await Promise.all(
    Array.from({ length: workerCount }, async () => {
      while (cursor < pending.length) {
        const shot = pending[cursor++];
        const source = shot.source || 'gaggimate';
        const key = getShotNotesKey(shot);
        const notes = chooseLoadedNotes(shot, await service.loadNotes(key, source), service);
        hydrated.set(`${source}:${shot.id}`, applyNotesRating(shot, notes));
      }
    }),
  );

  return visibleShots.map(shot => hydrated.get(`${shot.source || 'gaggimate'}:${shot.id}`) || shot);
}
