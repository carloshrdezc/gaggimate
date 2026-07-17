import { signal } from '@preact/signals';

// TODO: persist to sessionStorage so the comparison set survives page refresh within the same session (PRO-467)
// Currently in-memory only — resets on page refresh. Intentional for first version.
export const comparisonShots = signal([]); // array of shot objects, max 4

export const addToComparison = shot => {
  const current = comparisonShots.value;
  if (current.length >= 4) return;
  if (current.some(s => s.id === shot.id)) return;
  comparisonShots.value = [...current, shot];
};

export const removeFromComparison = shotId => {
  comparisonShots.value = comparisonShots.value.filter(s => s.id !== shotId);
};

export const clearComparison = () => {
  comparisonShots.value = [];
};

export const isInComparison = shotId => comparisonShots.value.some(s => s.id === shotId);
