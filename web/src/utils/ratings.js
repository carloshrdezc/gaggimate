function roundToTenth(value) {
  return Math.round(value * 10) / 10;
}

export function normalizeTenPointRating(value) {
  const parsed = typeof value === 'number' ? value : parseFloat(value);
  if (!Number.isFinite(parsed) || parsed <= 0) return 0;
  return Math.max(1, Math.min(10, roundToTenth(parsed)));
}

export function formatTenPointRating(value) {
  const normalized = normalizeTenPointRating(value);
  if (!normalized) return '\u2014';
  return `${normalized.toFixed(1).replace(/\.0$/, '')}/10`;
}

export function getRatingFillPercent(value) {
  const normalized = normalizeTenPointRating(value);
  // Round to avoid floating-point dust (e.g. 8.7/10*100 = 86.999…%) leaking
  // into the inline style width now that ratings carry one decimal.
  return `${Math.round((normalized / 10) * 1000) / 10}%`;
}
