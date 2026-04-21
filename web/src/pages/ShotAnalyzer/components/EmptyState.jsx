/**
 * EmptyState.jsx - Dark Precision restyle
 */
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { analyzerUiColors } from '../utils/analyzerUtils.js';
import { Spinner } from '../../../components/Spinner.jsx';
import { SourceMarker } from './SourceMarker.jsx';
import DeepDiveLogoRaw from '../assets/deepdive.svg?raw';

const deepDiveLogoMarkup = DeepDiveLogoRaw.replace(
  '<svg width="2048" height="2048" viewBox="0 0 2048 2048" xmlns="http://www.w3.org/2000/svg">',
  '<svg width="100%" height="100%" viewBox="0 0 2048 2048" xmlns="http://www.w3.org/2000/svg" preserveAspectRatio="xMidYMid meet" style="display:block;">',
).replaceAll('fill="#ffffff"', 'fill="currentColor"');

function DeepDiveLogoMark() {
  return (
    <div
      class="mx-auto opacity-15 [&>svg]:h-24 [&>svg]:w-24"
      style={{ color: 'var(--text-muted)' }}
      dangerouslySetInnerHTML={{ __html: deepDiveLogoMarkup }}
      aria-hidden="true"
    />
  );
}

const SourceRow = ({ icon, title, description }) => (
  <div class="flex items-start gap-3 py-3">
    <div class="flex size-8 shrink-0 items-center justify-center">
      {icon}
    </div>
    <div class="flex-1">
      <h3 class="text-sm font-semibold text-[--text-primary] mb-0.5">{title}</h3>
      <p class="text-xs text-[--text-muted] leading-relaxed">{description}</p>
    </div>
  </div>
);

export function EmptyState({ loading }) {
  if (loading) {
    return (
      <div class="flex items-center justify-center py-16">
        <div class="size-8 rounded-full border-2 border-[--accent] border-t-transparent animate-spin" />
      </div>
    );
  }

  return (
    <div class="flex flex-col items-center justify-center py-8">
      <div class="w-full max-w-lg rounded-xl p-6" style="background: var(--bg-elevated); border: 1px solid var(--border);">
        {/* Header */}
        <div class="text-center mb-6 pb-5" style="border-bottom: 1px solid var(--border);">
          <h2 class="text-xl font-semibold text-[--text-primary]">No Shot Loaded</h2>
          <p class="text-sm text-[--text-muted] mt-1">Import a shot file or select one from your library to start analyzing.</p>
        </div>

        {/* Supported Sources */}
        <p class="text-xs font-semibold text-[--text-muted] uppercase tracking-wider mb-1">Supported Sources</p>
        <div class="mb-4" style="border-bottom: 1px solid var(--border);">
          <SourceRow
            icon={<SourceMarker source="gaggimate" variant="large" />}
            title="GaggiMate (GM)"
            description="Your saved shots and profiles directly from the GaggiMate internal storage."
          />
          <div class="h-px w-full" style="background: var(--border);" />
          <SourceRow
            icon={<FontAwesomeIcon icon={faEye} class="text-lg text-[--text-muted]" />}
            title="Temporary View (VIEW)"
            description="Opens imported external shots and profiles temporarily without saving them to the browser library."
          />
          <div class="h-px w-full" style="background: var(--border);" />
          <SourceRow
            icon={<SourceMarker source="browser" variant="large" />}
            title="Local Browser Storage (WEB)"
            description="Stores imported external shots and profiles locally in this browser on this device. Not available in other browsers or devices."
          />
        </div>

        {/* Import Guidance */}
        <p class="text-xs font-semibold text-[--text-muted] uppercase tracking-wider mb-2">Import Guidance</p>
        <div class="text-xs text-[--text-muted] space-y-1.5 leading-relaxed">
          <p>Drag and drop files onto the status bar or use the import icons in the shot and profile badges.</p>
          <p>Use the status bar toggle to switch between <span class="text-[--text-primary] font-medium">View temporarily</span> and <span class="font-medium" style={`color: ${analyzerUiColors.sourceBadgeWebText};`}>Save to Browser</span> before importing.</p>
          <p>Bulk upload and download are supported.</p>
        </div>
      </div>

      <DeepDiveLogoMark />
    </div>
  );
}