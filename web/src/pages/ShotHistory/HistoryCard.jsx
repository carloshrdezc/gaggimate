import { useCallback, useState, useContext } from 'preact/hooks';
import { HistoryChart } from './HistoryChart.jsx';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faWeightScale } from '@fortawesome/free-solid-svg-icons/faWeightScale';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faUpload } from '@fortawesome/free-solid-svg-icons/faUpload';
import { faStar } from '@fortawesome/free-solid-svg-icons/faStar';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { faMinus } from '@fortawesome/free-solid-svg-icons/faMinus';
import { faMagnifyingGlassChart } from '@fortawesome/free-solid-svg-icons/faMagnifyingGlassChart';
import ShotNotesCard from './ShotNotesCard.jsx';
import { useConfirmAction } from '../../hooks/useConfirmAction.js';
import VisualizerUploadModal from '../../components/VisualizerUploadModal.jsx';
import { visualizerService } from '../../services/VisualizerService.js';
import { ApiServiceContext } from '../../services/ApiService.js';
import { Tooltip } from '../../components/Tooltip.jsx';
import { formatTenPointRating, getRatingFillPercent } from '../../utils/ratings.js';

function round2(v) {
  if (v == null || Number.isNaN(v)) return v;
  return Math.round((v + Number.EPSILON) * 100) / 100;
}

export default function HistoryCard({ shot, onDelete, onLoad, onNotesChanged }) {
  const apiService = useContext(ApiServiceContext);
  const [shotNotes, setShotNotes] = useState(shot.notes || null);
  const [expanded, setExpanded] = useState(false);
  const { armed: confirmDelete, armOrRun: confirmOrDelete } = useConfirmAction(4000);
  const [showUploadModal, setShowUploadModal] = useState(false);
  const [isUploading, setIsUploading] = useState(false);
  const [isExporting, setIsExporting] = useState(false);

  const date = new Date(shot.timestamp * 1000);
  const effectiveRating = shotNotes?.rating ?? shot.rating ?? 0;
  const hasSamples = Array.isArray(shot.samples) && shot.samples.length > 0;

  const onExport = useCallback(async () => {
    const download = prepareDownload('shot-' + shot.id + '.json');
    setIsExporting(true);
    try {
      const exportShot = !shot.loaded && onLoad ? await onLoad(shot) : shot;
      if (!exportShot?.loaded) throw new Error('Shot data is not available yet.');
      const exportData = { ...exportShot, notes: shotNotes };
      if (Array.isArray(exportData.samples)) {
        exportData.samples = exportData.samples.map(s => ({
          t: s.t, tt: round2(s.tt), ct: round2(s.ct), tp: round2(s.tp),
          cp: round2(s.cp), fl: round2(s.fl), tf: round2(s.tf),
          pf: round2(s.pf), vf: round2(s.vf), v: round2(s.v),
          ev: round2(s.ev), pr: round2(s.pr), systemInfo: s.systemInfo,
          phaseNumber: s.phaseNumber, phaseDisplayNumber: s.phaseDisplayNumber,
        }));
      }
      exportData.volume = round2(exportData.volume);
      downloadJson(exportData, 'shot-' + shot.id + '.json', download);
    } catch (error) {
      console.error('Failed to export shot:', error);
      download.fail(error);
      alert(`Shot export failed: ${error.message}`);
    } finally {
      setIsExporting(false);
    }
  }, [onLoad, shot, shotNotes]);

  const handleNotesLoaded = useCallback(notes => setShotNotes(notes), []);
  const handleNotesUpdate = useCallback(notes => {
    setShotNotes(notes);
    if (onNotesChanged) onNotesChanged(shot.id, notes, shot.source);
  }, [onNotesChanged, shot.id, shot.source]);

  const profileTitle = shot.beanName
    ? `${shot.profile || 'Unknown Profile'} \u2022 ${shot.beanName}`
    : shot.profile || 'Unknown Profile';

  const formattedDate = date.toLocaleDateString() + ' ' + date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

  const handleUpload = useCallback(async (username, password, rememberCredentials) => {
    setIsUploading(true);
    try {
      if (!visualizerService.validateShot(shot)) throw new Error('Shot data is invalid or incomplete');
      let profileData = null;
      if (shot.profileId && apiService) {
        try {
          const profileResponse = await apiService.request({ tp: 'req:profiles:load', id: shot.profileId });
          if (profileResponse.profile) profileData = profileResponse.profile;
        } catch (error) { console.warn('Failed to fetch profile data:', error); }
      }
      const shotWithNotes = { ...shot, notes: shotNotes };
      await visualizerService.uploadShot(shotWithNotes, username, password, profileData);
      alert('Shot uploaded successfully to visualizer.coffee!');
    } catch (error) {
      console.error('Upload failed:', error);
      alert(`Upload failed: ${error.message}`);
      throw error;
    } finally {
      setIsUploading(false);
    }
  }, [shot, shotNotes, apiService]);

  const canUpload = visualizerService.validateShot(shot);

  return (
    <div class="rounded-xl p-4 transition-all duration-150 hover:bg-[--bg-elevated]" style="border: 1px solid transparent;">
      {/* Header row */}
      <div class="flex flex-col gap-3">
        <div class="flex items-start gap-3">
          {/* Expand button */}
          <button
            onClick={() => {
              const next = !expanded;
              setExpanded(next);
              if (next && !shot.loaded && onLoad) onLoad(shot.id);
            }}
            class="shrink-0 rounded-lg p-2 text-[--text-muted] hover:text-[--text-primary] hover:bg-[--bg-glass] transition-all"
            aria-label={expanded ? 'Collapse shot details' : 'Expand shot details'}
          >
            <FontAwesomeIcon icon={expanded ? faMinus : faPlus} class="text-sm" />
          </button>

          {/* Shot info */}
          <div class="min-w-0 flex-grow">
            <div class="flex flex-col gap-2 md:flex-row md:items-start md:justify-between">
              <div>
                <h3 class="text-sm font-semibold text-[--text-primary] truncate">{profileTitle}</h3>
                <p class="text-xs text-[--text-muted] mt-0.5">
                  #{shot.id} · {formattedDate}
                </p>
                {expanded && shot.loaded && hasSamples && shot.samples[0].systemInfo && (
                  <p class="text-xs text-[--text-muted] italic mt-0.5">
                    Brewed by {shot.samples[0].systemInfo.shotStartedVolumetric ? 'Weight' : 'Time'}
                  </p>
                )}
              </div>

              {/* Badges + actions */}
              <div class="flex items-center gap-2 flex-wrap">
                <span class={`text-xs px-2 py-0.5 rounded-full font-medium ${shot.source === 'browser' ? 'text-purple-400' : 'text-blue-400'}`} style={shot.source === 'browser' ? 'background: rgba(168,85,247,0.12);' : 'background: rgba(59,130,246,0.12);'}>
                  {shot.source === 'browser' ? 'Imported' : 'Device'}
                </span>
                {shot.incomplete && (
                  <span class="text-xs px-2 py-0.5 rounded-full text-[--warning] font-medium" style="background: rgba(234,179,8,0.12);">
                    INCOMPLETE
                  </span>
                )}

                {/* Action buttons */}
                <div class="flex items-center gap-1">
                  <Tooltip content={isExporting ? 'Exporting...' : 'Export'}>
                    <button
                      disabled={isExporting}
                      onClick={onExport}
                      class="p-2 rounded-lg text-[--text-muted] hover:text-[--accent] hover:bg-[--bg-glass] transition-all disabled:opacity-40"
                      aria-label="Export shot"
                    >
                      <FontAwesomeIcon icon={faFileExport} />
                    </button>
                  </Tooltip>
                  <Tooltip content="Open in Analyzer">
                    <a
                      href={`/analyzer/internal/${shot.id}`}
                      class="p-2 rounded-lg text-[--text-muted] hover:text-[--accent] hover:bg-[--bg-glass] transition-all"
                      aria-label="Open in Analyzer"
                    >
                      <FontAwesomeIcon icon={faMagnifyingGlassChart} />
                    </a>
                  </Tooltip>
                  <Tooltip content={canUpload ? 'Upload to Visualizer.coffee' : 'Load shot data first'}>
                    <button
                      onClick={() => setShowUploadModal(true)}
                      disabled={!canUpload}
                      class={`p-2 rounded-lg transition-all ${canUpload ? 'text-[--success] hover:bg-[--success]/10' : 'text-[--text-muted] cursor-not-allowed opacity-40'}`}
                      aria-label="Upload to visualizer"
                    >
                      <FontAwesomeIcon icon={faUpload} />
                    </button>
                  </Tooltip>
                  <Tooltip content={confirmDelete ? 'Click to confirm' : 'Delete'}>
                    <button
                      onClick={() => confirmOrDelete(() => onDelete(shot.id))}
                      class={`p-2 rounded-lg transition-all ${confirmDelete ? 'bg-[--error] text-white' : 'text-[--error] hover:bg-[--error]/10'}`}
                      aria-label={confirmDelete ? 'Confirm deletion' : 'Delete shot'}
                    >
                      <FontAwesomeIcon icon={faTrashCan} />
                      {confirmDelete && <span class="ml-1 text-xs hidden sm:inline">Confirm</span>}
                    </button>
                  </Tooltip>
                </div>
              </div>
            </div>

            {/* Stats row */}
            <div class="flex flex-wrap items-center gap-4 mt-2 text-xs text-[--text-secondary]">
              <div class="flex items-center gap-1">
                <FontAwesomeIcon icon={faClock} class="text-[--text-muted]" />
                <span class="font-data">{(shot.duration / 1000).toFixed(1)}s</span>
              </div>
              {shot.volume && shot.volume > 0 && (
                <div class="flex items-center gap-1">
                  <FontAwesomeIcon icon={faWeightScale} class="text-[--text-muted]" />
                  <span class="font-data">{round2(shot.volume)}g</span>
                </div>
              )}
              {effectiveRating && effectiveRating > 0 ? (
                <div class="flex items-center gap-1">
                  <FontAwesomeIcon icon={faStar} class="text-yellow-400" />
                  <div class="relative inline-flex text-sm leading-none">
                    <div class="text-[--text-muted]">★★★★★</div>
                    <div class="absolute inset-y-0 left-0 overflow-hidden whitespace-nowrap text-yellow-400" style={{ width: getRatingFillPercent(effectiveRating) }}>
                      ★★★★★
                    </div>
                  </div>
                  <span class="text-[--text-primary] font-medium">{formatTenPointRating(effectiveRating)}</span>
                </div>
              ) : (
                <div class="flex items-center gap-1 text-[--text-muted]">
                  <FontAwesomeIcon icon={faStar} />
                  <span>Not rated</span>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* Expanded content */}
        {expanded && (
          <div class="mt-3 pt-3" style="border-top: 1px solid var(--border);">
            {!shot.loaded && (
              <div class="flex items-center justify-center py-8">
                <span class="text-sm text-[--text-muted]">Loading shot data...</span>
              </div>
            )}
            {shot.loaded && hasSamples && <HistoryChart shot={shot} />}
            {shot.loaded && (
              <ShotNotesCard
                shot={shot}
                onNotesLoaded={handleNotesLoaded}
                onNotesUpdate={handleNotesUpdate}
              />
            )}
            {shot.loaded && !hasSamples && (
              <p class="text-sm text-[--text-muted] mt-3">
                This backup contains shot details and notes, but not the full sample trace.
              </p>
            )}
          </div>
        )}
      </div>

      <VisualizerUploadModal
        isOpen={showUploadModal}
        onClose={() => setShowUploadModal(false)}
        onUpload={handleUpload}
        isUploading={isUploading}
        shotInfo={{ profile: shot.profile, timestamp: shot.timestamp, duration: shot.duration, volume: shot.volume }}
      />
    </div>
  );
}