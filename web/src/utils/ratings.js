function roundToQuarter(value) {
  return Math.round(value * 4) / 4;
}

export function normalizeTenPointRating(value) {
  const parsed = typeof value === 'number' ? value : parseFloat(value);
  if (!Number.isFinite(parsed) || parsed <= 0) return 0;
  return Math.max(1, Math.min(10, roundToQuarter(parsed)));
}

export function formatTenPointRating(value) {
  const normalized = normalizeTenPointRating(value);
  if (!normalized) return '\u2014';
  return `${normalized.toFixed(2).replace(/\.?0+$/, '')}/10`;
}

export function getRatingFillPercent(value) {
  const normalized = normalizeTenPointRating(value);
  return `${(normalized / 10) * 100}%`;
}
