import { useContext } from 'preact/hooks';
import { ApiServiceContext, machine } from '../services/ApiService.js';
import { DataValue } from './DataValue.jsx';

const MODE_DOT_COLORS = {
  0: 'bg-[--text-muted]',
  1: 'bg-[--accent]',
  2: 'bg-[--warning]',
  3: 'bg-[--error]',
  4: 'bg-[--text-secondary]',
};

const MODE_LABELS = ['Standby', 'Brew', 'Steam', 'Water', 'Grind'];

function formatTemp(temp) {
  return Number.isFinite(temp) ? temp.toFixed(1) : '0.0';
}

function formatWeight(weight) {
  return Number.isFinite(weight) ? weight.toFixed(1) : '0.0';
}

function formatTime(ms) {
  const seconds = Math.floor(ms / 1000);
  const mins = Math.floor(seconds / 60);
  const secs = seconds % 60;
  return `${mins}:${secs.toString().padStart(2, '0')}`;
}

export function StatusStrip() {
  const status = machine.value.status;
  const processInfo = status.process;

  const mode = status.mode;
  const isActive = processInfo?.a;
  const isFinished = processInfo && !processInfo.a && processInfo.e > 0;

  // Current state label
  let stateLabel = MODE_LABELS[mode] || 'Unknown';
  if (isActive) stateLabel = 'Brewing';
  if (isFinished) stateLabel = 'Finished';

  // Calculate elapsed time
  const elapsedMs = processInfo?.e || 0;

  // Current weight display
  const currentWeight = status.currentWeight || 0;
  const targetWeight = status.targetWeight || 0;

  return (
    <div class="flex items-center gap-4 px-4 py-2 bg-[--bg-elevated] rounded-lg border border-[--border]">
      {/* Mode dot */}
      <span class={`size-2.5 rounded-full ${MODE_DOT_COLORS[mode]}`} />

      {/* State */}
      <span class="text-sm font-medium text-[--text-primary]">{stateLabel}</span>

      {/* Separator */}
      <span class="text-[--text-muted]">·</span>

      {/* Temperature */}
      <DataValue
        value={formatTemp(status.currentTemperature)}
        unit="°C"
        size="label"
        label="Temp"
      />

      {/* Separator */}
      <span class="text-[--text-muted]">·</span>

      {/* Weight (if available) */}
      {status.volumetricAvailable && targetWeight > 0 && (
        <>
          <DataValue
            value={`${formatWeight(currentWeight)} / ${formatWeight(targetWeight)}`}
            unit="g"
            size="label"
          />
          {/* Separator */}
          <span class="text-[--text-muted]">·</span>
        </>
      )}

      {/* Time (if process active) */}
      {(isActive || isFinished) && (
        <DataValue
          value={formatTime(elapsedMs)}
          unit=""
          size="label"
          label="Time"
        />
      )}
    </div>
  );
}