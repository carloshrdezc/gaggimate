/**
 * DataValue - Monospace value + unit display
 * Sizes: hero (32px), card (20px), label (14px)
 */
export function DataValue({ value, unit, size = 'card', label }) {
  const sizeClasses = {
    hero: 'text-3xl font-data',
    card: 'text-xl font-data',
    label: 'text-sm font-data',
  };

  return (
    <div class="inline-flex flex-col">
      {label && <span class="text-[--text-muted] text-xs uppercase tracking-wider mb-0.5">{label}</span>}
      <span class={`${sizeClasses[size]} text-[--text-primary]`}>
        <span class="value">{value}</span>
        <span class="text-[--text-secondary] ml-1">{unit}</span>
      </span>
    </div>
  );
}