import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCircle } from '@fortawesome/free-solid-svg-icons/faCircle';
import { faFire } from '@fortawesome/free-solid-svg-icons/faFire';
import { faDroplet } from '@fortawesome/free-solid-svg-icons/faDroplet';
import { faMugHot } from '@fortawesome/free-solid-svg-icons/faMugHot';

const MODE_CONFIG = {
  0: { label: 'Standby', icon: faCircle, color: 'text-[--text-muted]' },
  1: { label: 'Brew', icon: faFire, color: 'text-[--accent]' },
  2: { label: 'Steam', icon: faDroplet, color: 'text-[--warning]' },
  3: { label: 'Water', icon: faDroplet, color: 'text-[--error]' },
  4: { label: 'Grind', icon: faMugHot, color: 'text-[--text-secondary]' },
};

export function ModePill({ mode }) {
  const config = MODE_CONFIG[mode] || MODE_CONFIG[0];
  const isActive = mode !== 0;

  return (
    <div
      class={`inline-flex items-center gap-2 px-3 py-1.5 rounded-full border transition-all ${
        isActive
          ? 'border-[--accent] bg-[--accent-glow]'
          : 'border-[--border] bg-[--bg-elevated]'
      }`}
    >
      <FontAwesomeIcon icon={config.icon} class={`text-sm ${config.color}`} />
      <span class={`text-xs font-medium ${isActive ? 'text-[--accent]' : 'text-[--text-secondary]'}`}>
        {config.label}
      </span>
    </div>
  );
}