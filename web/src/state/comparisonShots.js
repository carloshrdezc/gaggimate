import { signal } from '@preact/signals';

// TODO: persist to sessionStorage so the comparison set survives page refresh within the same session (PRO-467)
// Currently in-memory only — resets on page refresh. Intentional for first version.
export const comparisonShots = signal([]); // array of shot objects, max 4

export const getComparisonShotIdentity = shot => {
  const source = shot.source || 'gaggimate';
  const key = source === 'browser' ? shot.storageKey || shot.name || shot.id : shot.id;
  return `${source}:${String(key || '')}`;
};

export const addToComparison = shot => {
  const current = comparisonShots.value;
  if (current.length >= 4) return;
  const identity = getComparisonShotIdentity(shot);
  if (current.some(s => getComparisonShotIdentity(s) === identity)) return;
  comparisonShots.value = [...current, shot];
};

export const removeFromComparison = shot => {
  const identity = getComparisonShotIdentity(shot);
  comparisonShots.value = comparisonShots.value.filter(s => getComparisonShotIdentity(s) !== identity);
};

export const clearComparison = () => {
  comparisonShots.value = [];
};

export const isInComparison = shot => {
  const identity = getComparisonShotIdentity(shot);
  return comparisonShots.value.some(s => getComparisonShotIdentity(s) === identity);
};
