import { useContext, useState } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSliders, faCog, faUser, faChevronDown } from '@fortawesome/free-solid-svg-icons';
import { ApiServiceContext, machine } from '../services/ApiService.js';
import { ModePill } from './ModePill.jsx';

const MODE_LABELS = ['Standby', 'Brew', 'Steam', 'Water', 'Grind'];
const MODE_DOT_COLORS = [
  'bg-[--text-muted]',
  'bg-[--accent]',
  'bg-[--warning]',
  'bg-[--error]',
  'bg-[--text-secondary]',
];

export function TopBar() {
  const api = useContext(ApiServiceContext);
  const mode = machine.value.status.mode;
  const connected = machine.value.connected;
  const [modePopoverOpen, setModePopoverOpen] = useState(false);

  const handleModeSelect = (newMode) => {
    try {
      api.send({ tp: 'req:change-mode', mode: newMode });
      setModePopoverOpen(false);
    } catch (err) {
      console.error('Failed to change mode:', err);
    }
  };

  return (
    <header class="fixed top-0 left-0 right-0 z-50 h-12 bg-[--bg-base] border-b border-[--border]">
      <div class="h-full max-w-[1400px] mx-auto px-4 flex items-center justify-between">

        {/* Logo */}
        <div class="flex items-center gap-2">
          <span class="size-8 grid place-items-center rounded-lg bg-[--accent] text-[--bg-base] font-bold text-lg">
            G
          </span>
          <span class="font-semibold text-lg tracking-wide hidden sm:inline">Gaggimate</span>
        </div>

        {/* Mode Pill - Center */}
        <div class="absolute left-1/2 -translate-x-1/2">
          <ModePill mode={mode} />
        </div>

        {/* Right side - Status + Icons */}
        <div class="flex items-center gap-3">

          {/* Connection indicator */}
          <div class="flex items-center gap-1.5">
            <span class={`size-2 rounded-full ${connected ? 'bg-[--success]' : 'bg-[--warning]'}`} />
            <span class="text-xs text-[--text-secondary] hidden sm:inline">
              {connected ? 'Online' : 'Offline'}
            </span>
          </div>

          {/* Mode Selector - clickable */}
          <div class="relative">
            <button
              onClick={() => setModePopoverOpen(!modePopoverOpen)}
              class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-[--bg-elevated] border border-[--border] text-sm text-[--text-secondary] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
            >
              <span class={`size-2 rounded-full ${MODE_DOT_COLORS[mode]}`} />
              <span class="hidden sm:inline">{MODE_LABELS[mode] || 'Unknown'}</span>
              <FontAwesomeIcon icon={faChevronDown} class="text-xs opacity-50" />
            </button>

            {/* Mode Popover */}
            {modePopoverOpen && (
              <div class="absolute top-full right-0 mt-2 w-40 rounded-xl border border-[--border] bg-[--bg-elevated] p-2 shadow-lg">
                <div class="space-y-1">
                  {MODE_LABELS.map((label, index) => (
                    <button
                      key={index}
                      onClick={() => handleModeSelect(index)}
                      class={`w-full flex items-center gap-2 px-3 py-2 rounded-lg text-sm transition-all ${
                        index === mode
                          ? 'bg-[--accent]/10 text-[--accent] border border-[--accent]/30'
                          : 'text-[--text-secondary] hover:bg-[--bg-glass] hover:text-[--text-primary] border border-transparent'
                      }`}
                    >
                      <span class={`size-2 rounded-full ${MODE_DOT_COLORS[index]}`} />
                      {label}
                    </button>
                  ))}
                </div>
              </div>
            )}
          </div>

          {/* Settings icon */}
          <a
            href="/settings"
            class="p-2 rounded-lg text-[--text-secondary] hover:text-[--text-primary] hover:bg-[--bg-elevated] transition-all"
            aria-label="Settings"
          >
            <FontAwesomeIcon icon={faCog} class="text-lg" />
          </a>

          {/* User avatar */}
          <button
            class="size-8 rounded-full bg-[--bg-elevated] border border-[--border] flex items-center justify-center text-[--text-secondary] hover:border-[--border-active] transition-all"
            aria-label="User profile"
          >
            <FontAwesomeIcon icon={faUser} class="text-sm" />
          </button>
        </div>
      </div>

      {/* Click outside to close popover */}
      {modePopoverOpen && (
        <div
          class="fixed inset-0 z-[-1]"
          onClick={() => setModePopoverOpen(false)}
        />
      )}
    </header>
  );
}