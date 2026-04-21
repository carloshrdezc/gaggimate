import { useContext } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSliders, faCog, faUser } from '@fortawesome/free-solid-svg-icons';
import { ApiServiceContext, machine } from '../services/ApiService.js';
import { ModePill } from './ModePill.jsx';

export function TopBar() {
  const mode = machine.value.status.mode;
  const connected = machine.value.connected;

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
        <div class="flex items-center gap-4">
          {/* Connection indicator */}
          <div class="flex items-center gap-1.5">
            <span class={`size-2 rounded-full ${connected ? 'bg-[--success]' : 'bg-[--warning]'}`} />
            <span class="text-xs text-[--text-secondary] hidden sm:inline">
              {connected ? 'Online' : 'Offline'}
            </span>
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
    </header>
  );
}