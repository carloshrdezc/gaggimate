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

export async function hydrateShotRatingsFromNotes(
  shots,
  service,
  { limit = DEFAULT_HYDRATION_LIMIT, concurrency = DEFAULT_HYDRATION_CONCURRENCY } = {},
) {
  const visibleShots = Array.isArray(shots) ? shots.slice(0, limit) : [];
  const hydrated = new Map();
  const pending = visibleShots.filter(shot => {
    if (!shot) return false;
    if (shot.notes) {
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
        const notes = await service.loadNotes(key, source);
        hydrated.set(`${source}:${shot.id}`, applyNotesRating(shot, notes));
      }
    }),
  );

  return visibleShots.map(shot => hydrated.get(`${shot.source || 'gaggimate'}:${shot.id}`) || shot);
}
