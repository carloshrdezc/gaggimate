import {
  CategoryScale,
  Chart,
  Filler,
  Legend,
  LinearScale,
  LineController,
  LineElement,
  PointElement,
  TimeScale,
} from 'chart.js';
import 'chartjs-adapter-dayjs-4/dist/chartjs-adapter-dayjs-4.esm';
import { ExtendedProfileChart } from '../../components/ExtendedProfileChart.jsx';
import { useConfirmAction } from '../../hooks/useConfirmAction.js';
import { ProfileAddCard } from './ProfileAddCard.jsx';
import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { computed } from '@preact/signals';
import { Spinner } from '../../components/Spinner.jsx';
import Card from '../../components/Card.jsx';
import {
  buildExportPayload,
  formatRestoreSummary,
  aggregateImportFiles,
  formatFileAggregateWarning,
  runRestore,
} from './migrationTransfer.js';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArrowUp } from '@fortawesome/free-solid-svg-icons/faArrowUp';
import { faArrowDown } from '@fortawesome/free-solid-svg-icons/faArrowDown';
import { faAnglesUp } from '@fortawesome/free-solid-svg-icons/faAnglesUp';
import { faAnglesDown } from '@fortawesome/free-solid-svg-icons/faAnglesDown';
import { faStar } from '@fortawesome/free-solid-svg-icons/faStar';
import { faPen } from '@fortawesome/free-solid-svg-icons/faPen';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faCopy } from '@fortawesome/free-solid-svg-icons/faCopy';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';
import { ImportButton } from '../../components/ImportButton.jsx';
import { faEllipsisVertical } from '@fortawesome/free-solid-svg-icons/faEllipsisVertical';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';
import { ConfirmButton } from '../../components/ConfirmButton.jsx';
import { Tooltip } from '../../components/Tooltip.jsx';
import { faTemperatureFull } from '@fortawesome/free-solid-svg-icons/faTemperatureFull';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faScaleBalanced } from '@fortawesome/free-solid-svg-icons/faScaleBalanced';
import { faSearch } from '@fortawesome/free-solid-svg-icons/faSearch';
import { buildStatisticsProfileHref } from '../Statistics/utils/statisticsRoute.js';

Chart.register(
  LineController,
  TimeScale,
  LinearScale,
  CategoryScale,
  PointElement,
  LineElement,
  Filler,
  Legend,
);

const PhaseLabels = {
  preinfusion: 'Pre-Infusion',
  brew: 'Brew',
};

const connected = computed(() => machine.value.connected);

function ProfileCard({
  data,
  onDelete,
  onSelect,
  onFavorite,
  onUnfavorite,
  onDuplicate,
  favoriteDisabled,
  unfavoriteDisabled,
  onMoveUp,
  onMoveDown,
  onMoveToTop,
  onMoveToBottom,
  isFirst,
  isLast,
}) {
  const { armed: confirmDelete, armOrRun: confirmOrDelete } = useConfirmAction(4000);
  const bookmarkClass = data.favorite ? 'text-[var(--color-warning,#d4a843)]' : 'text-[var(--text-disabled,#666)]';
  const typeText = data.type === 'pro' ? 'Pro' : 'Simple';
  const favoriteToggleDisabled = data.favorite ? unfavoriteDisabled : favoriteDisabled;
  const favoriteToggleClass = favoriteToggleDisabled ? 'opacity-40 cursor-not-allowed' : '';

  const onFavoriteToggle = useCallback(() => {
    if (data.favorite && !unfavoriteDisabled) onUnfavorite(data.id);
    else if (!data.favorite && !favoriteDisabled) onFavorite(data.id);
  }, [data.favorite, unfavoriteDisabled, favoriteDisabled, onUnfavorite, onFavorite, data.id]);

  const onDownload = useCallback(() => {
    // Keep `id` in the export so a re-imported profile remains addressable
    // (deletable/selectable/favoritable) on the device. Only `selected` and
    // `favorite` are device-local state and must not be exported.
    const { selected, favorite, ...profileData } = data;
    const filename = `profile-${data.id}.json`;
    const prepared = prepareDownload(filename);

    if (!prepared.targetWindow) {
      console.error('Failed to create download window - popup blocker may be active');
      alert('Download failed: Please allow popups for this site and try again.');
      return;
    }

    downloadJson(profileData, filename, prepared);
  }, [data]);
  const statsHref = buildStatisticsProfileHref({ source: 'gaggimate', profileName: data.label });

  const [detailsCollapsed, setDetailsCollapsed] = useState(true);
  const onToggleDetails = useCallback(() => setDetailsCollapsed(v => !v), []);
  const chevronRotation = detailsCollapsed ? '' : 'rotate-90';
  const detailsSectionId = `profile-${data.id}-summary`;

  const totalDurationSeconds = Array.isArray(data?.phases)
    ? data.phases.reduce((sum, p) => sum + (Number.isFinite(p?.duration) ? p.duration : 0), 0)
    : 0;

  const kebabRef = useRef(null);
  const popoverRef = useRef(null);
  const [menuOpen, setMenuOpen] = useState(false);

  const closeMenu = useCallback(() => setMenuOpen(false), []);

  const toggleMenu = useCallback(
    e => {
      e?.preventDefault?.();
      setMenuOpen(v => !v);
    },
    [],
  );

  useEffect(() => {
    if (!menuOpen) return;
    const handleClickOutside = e => {
      if (popoverRef.current && !popoverRef.current.contains(e.target) && !kebabRef.current.contains(e.target)) {
        setMenuOpen(false);
      }
    };
    const handleKeyDown = e => { if (e.key === 'Escape') setMenuOpen(false); };
    document.addEventListener('mousedown', handleClickOutside);
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [menuOpen]);

  return (
    <Card sm={12} role='listitem'>
      <div className='flex flex-row items-center' role='group' aria-labelledby={`profile-${data.id}-title`}>
        <div className='flex flex-grow flex-col overflow-hidden'>
          <div className='flex flex-row items-center gap-4'>
            {/* Checkbox */}
            <label className='cursor-pointer'>
              <input
                checked={data.selected}
                type='checkbox'
                onClick={() => onSelect(data.id)}
                className='nd-checkbox'
                aria-label={`Select ${data.label} profile`}
              />
            </label>

            {/* Label */}
            <div className='flex-1 min-w-0'>
              <span
                id={`profile-${data.id}-title`}
                className='font-nd-mono text-[16px] text-[var(--text-primary,#e8e8e8)] truncate block'
              >
                {data.label}
              </span>
              <div className='flex items-center gap-2 mt-1'>
                <span
                  className='font-nd-mono text-[11px] text-[var(--text-secondary,#999)] uppercase tracking-[0.08em]'
                  aria-label={`Profile type: ${typeText}`}
                >
                  {typeText}
                </span>
                <button
                  onClick={onToggleDetails}
                  className='nd-action-btn'
                  style={{ width: '24px', height: '24px' }}
                  aria-label={`${detailsCollapsed ? 'Show' : 'Hide'} details for ${data.label}`}
                  aria-expanded={!detailsCollapsed}
                  aria-controls={detailsSectionId}
                >
                  <FontAwesomeIcon icon={faChevronRight} className={`text-[10px] transition-transform ${chevronRotation}`} />
                </button>
              </div>
            </div>

            {/* Actions */}
            <div className='flex items-center gap-2'>
              {/* Mobile: Popover */}
              <div className='sm:hidden relative'>
                <button
                  ref={kebabRef}
                  onClick={toggleMenu}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label={`Open actions menu for ${data.label} profile`}
                  aria-haspopup='menu'
                  aria-expanded={menuOpen}
                  aria-controls={`profile-${data.id}-menu`}
                >
                  <FontAwesomeIcon icon={faEllipsisVertical} />
                </button>
                <div
                  id={`profile-${data.id}-menu`}
                  ref={popoverRef}
                  role='menu'
                  className={`nd-card absolute left-0 top-full z-50 mt-2 w-56 p-2 ${menuOpen ? 'block' : 'hidden'}`}
                >
                  <ul className='space-y-1'>
                    <li>
                      <button
                        onClick={() => { onFavoriteToggle(); closeMenu(); }}
                        disabled={favoriteToggleDisabled}
                        className={`w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded flex items-center gap-2 ${favoriteToggleDisabled ? 'opacity-40' : 'hover:bg-[rgba(255,255,255,0.04)]'}`}
                        aria-pressed={data.favorite}
                      >
                        <FontAwesomeIcon icon={faStar} className={bookmarkClass} />
                        {data.favorite ? 'Unfavorite' : 'Favorite'}
                      </button>
                    </li>
                    <li>
                      <a
                        href={`/profiles/${data.id}`}
                        onClick={closeMenu}
                        className='block font-nd-mono text-[13px] px-3 py-2 rounded hover:bg-[rgba(255,255,255,0.04)]'
                      >
                        <FontAwesomeIcon icon={faPen} className='mr-2' />
                        Edit
                      </a>
                    </li>
                    <li>
                      <a
                        href={statsHref}
                        onClick={closeMenu}
                        className='block font-nd-mono text-[13px] px-3 py-2 rounded hover:bg-[rgba(255,255,255,0.04)]'
                      >
                        <FontAwesomeIcon icon={faChartSimple} className='mr-2' />
                        Statistics
                      </a>
                    </li>
                    <li>
                      <button
                        onClick={() => { onDownload(); closeMenu(); }}
                        className='w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded hover:bg-[rgba(255,255,255,0.04)]'
                      >
                        <FontAwesomeIcon icon={faFileExport} className='mr-2' />
                        Export
                      </button>
                    </li>
                    <li>
                      <button
                        onClick={() => { onMoveToTop(data.id); closeMenu(); }}
                        disabled={isFirst}
                        className={`w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded flex items-center gap-2 ${isFirst ? 'opacity-40 cursor-not-allowed' : 'hover:bg-[rgba(255,255,255,0.04)]'}`}
                      >
                        <FontAwesomeIcon icon={faAnglesUp} />
                        Move to Top
                      </button>
                    </li>
                    <li>
                      <button
                        onClick={() => { onMoveToBottom(data.id); closeMenu(); }}
                        disabled={isLast}
                        className={`w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded flex items-center gap-2 ${isLast ? 'opacity-40 cursor-not-allowed' : 'hover:bg-[rgba(255,255,255,0.04)]'}`}
                      >
                        <FontAwesomeIcon icon={faAnglesDown} />
                        Move to Bottom
                      </button>
                    </li>
                    <li>
                      <button
                        onClick={() => { onDuplicate(data.id); closeMenu(); }}
                        className='w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded hover:bg-[rgba(255,255,255,0.04)]'
                      >
                        <FontAwesomeIcon icon={faCopy} className='mr-2' />
                        Duplicate
                      </button>
                    </li>
                    <li>
                      <button
                        onClick={() => { confirmOrDelete(() => { onDelete(data.id); closeMenu(); }); }}
                        className={`w-full text-left font-nd-mono text-[13px] px-3 py-2 rounded ${confirmDelete ? 'bg-[var(--color-error,#d71921)] text-white' : 'hover:bg-[rgba(255,255,255,0.04)]'}`}
                      >
                        <FontAwesomeIcon icon={faTrashCan} className='mr-2' />
                        {confirmDelete ? 'Confirm' : 'Delete'}
                      </button>
                    </li>
                  </ul>
                </div>
              </div>

              {/* Desktop: inline actions */}
              <div className='hidden sm:flex items-center gap-2'>
                <button
                  onClick={onFavoriteToggle}
                  disabled={favoriteToggleDisabled}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label={data.favorite ? 'Remove from favorites' : 'Add to favorites'}
                  aria-pressed={data.favorite}
                >
                  <FontAwesomeIcon icon={faStar} className={bookmarkClass} />
                </button>
                <a
                  href={`/profiles/${data.id}`}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label='Edit profile'
                >
                  <FontAwesomeIcon icon={faPen} />
                </a>
                <a
                  href={statsHref}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label='View statistics'
                >
                  <FontAwesomeIcon icon={faChartSimple} />
                </a>
                <button
                  onClick={onDownload}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label='Export profile'
                >
                  <FontAwesomeIcon icon={faFileExport} />
                </button>
                <button
                  onClick={() => onDuplicate(data.id)}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label='Duplicate profile'
                >
                  <FontAwesomeIcon icon={faCopy} />
                </button>
                <button
                  onClick={() => { confirmOrDelete(() => onDelete(data.id)); }}
                  className='nd-action-btn'
                  style={{ width: '36px', height: '36px' }}
                  aria-label={confirmDelete ? 'Confirm delete' : 'Delete profile'}
                >
                  <FontAwesomeIcon icon={faTrashCan} className={confirmDelete ? 'text-[var(--color-error,#d71921)]' : ''} />
                </button>
              </div>
            </div>
          </div>

          {/* Details section */}
          {!detailsCollapsed && (
            <div id={detailsSectionId} className='mt-3 pt-3 border-t border-[var(--home-border,#222)]'>
              <div className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)] mb-2'>
                {data.description}
              </div>
              <div className='flex flex-wrap gap-3'>
                <span className='font-nd-mono text-[11px] text-[var(--text-secondary,#999)] flex items-center gap-1'>
                  <FontAwesomeIcon icon={faTemperatureFull} className='text-[10px]' />
                  {data.temperature}°C
                </span>
                <span className='font-nd-mono text-[11px] text-[var(--text-secondary,#999)] flex items-center gap-1'>
                  <FontAwesomeIcon icon={faClock} className='text-[10px]' />
                  {totalDurationSeconds}s
                </span>
                {data.phases.length > 0 &&
                  data.phases.at(-1)?.targets?.some(target => target.type === 'volumetric') && (
                    <span className='font-nd-mono text-[11px] text-[var(--text-secondary,#999)] flex items-center gap-1'>
                      <FontAwesomeIcon icon={faScaleBalanced} className='text-[10px]' />
                      {`${data.phases.at(-1).targets.find(target => target.type === 'volumetric').value}g`}
                    </span>
                  )}
                {data.phases.length > 0 && (
                  <span className='font-nd-mono text-[11px] text-[var(--text-secondary,#999)]'>
                    {data.phases.length} phase{data.phases.length === 1 ? '' : 's'}
                  </span>
                )}
              </div>
            </div>
          )}

          {/* Chart row */}
          <div className='flex items-center gap-2 mt-3 pt-3 border-t border-[var(--home-border,#222)]'>
            <div className='flex flex-col gap-1'>
              <button
                onClick={() => onMoveToTop(data.id)}
                disabled={isFirst}
                className='nd-action-btn'
                style={{ width: '28px', height: '28px' }}
                aria-label={`Move ${data.label} to top`}
              >
                <FontAwesomeIcon icon={faAnglesUp} className='text-[10px]' />
              </button>
              <button
                onClick={() => onMoveToBottom(data.id)}
                disabled={isLast}
                className='nd-action-btn'
                style={{ width: '28px', height: '28px' }}
                aria-label={`Move ${data.label} to bottom`}
              >
                <FontAwesomeIcon icon={faAnglesDown} className='text-[10px]' />
              </button>
              <div className='h-2' />
              <button
                onClick={() => onMoveUp(data.id)}
                disabled={isFirst}
                className='nd-action-btn'
                style={{ width: '28px', height: '28px' }}
                aria-label={`Move ${data.label} up one position`}
              >
                <FontAwesomeIcon icon={faArrowUp} className='text-[10px]' />
              </button>
              <button
                onClick={() => onMoveDown(data.id)}
                disabled={isLast}
                className='nd-action-btn'
                style={{ width: '28px', height: '28px' }}
                aria-label={`Move ${data.label} down one position`}
              >
                <FontAwesomeIcon icon={faArrowDown} className='text-[10px]' />
              </button>
            </div>
            <div className='flex-1 min-w-0'>
              {data.type === 'pro' ? (
                <ExtendedProfileChart data={data} className='max-h-36' />
              ) : (
                <SimpleContent data={data} />
              )}
            </div>
          </div>
        </div>
      </div>
    </Card>
  );
}

function SimpleContent({ data }) {
  return (
    <div className='flex flex-row items-center gap-2 overflow-x-auto' role='list' aria-label='Brew phases'>
      {data.phases.map((phase, i) => (
        <div key={i} className='flex flex-row items-center gap-2' role='listitem'>
          {i > 0 && <FontAwesomeIcon icon={faChevronRight} className='text-[var(--text-disabled,#666)] text-[10px]' />}
          <SimpleStep
            phase={phase.phase}
            type={phase.name}
            duration={phase.duration}
            targets={phase.targets || []}
          />
        </div>
      ))}
    </div>
  );
}

function SimpleStep({ phase, type, duration, targets }) {
  return (
    <div className='nd-card p-3 min-w-[80px]'>
      <div className='font-nd-mono text-[11px] text-[var(--text-primary,#e8e8e8)] uppercase tracking-[0.06em]'>
        {PhaseLabels[phase] || phase}
      </div>
      <div className='font-nd-mono text-[10px] text-[var(--text-disabled,#666)] mt-0.5'>
        {type}
      </div>
      <div className='font-nd-mono text-[10px] text-[var(--text-secondary,#999)] mt-1'>
        {targets.length === 0 && <span>{duration}s</span>}
        {targets.map((t, i) => (
          <span key={i}>Exit {t.value}{t.type === 'volumetric' ? 'g' : ''}</span>
        ))}
      </div>
    </div>
  );
}

export function ProfileList() {
  const apiService = useContext(ApiServiceContext);
  const [profiles, setProfiles] = useState([]);
  const [loading, setLoading] = useState(true);
  const [searchTerm, setSearchTerm] = useState('');
  const [activeTab, setActiveTab] = useState('extraction');
  const favoriteCount = profiles.map(p => (p.favorite ? 1 : 0)).reduce((a, b) => a + b, 0);
  const unfavoriteDisabled = favoriteCount <= 1;
  const favoriteDisabled = favoriteCount >= 10;
  const hasUtilityProfiles = useMemo(() => profiles.some(p => p.utility), [profiles]);

  useEffect(() => {
    if (!hasUtilityProfiles) {
      setActiveTab('extraction');
    }
  }, [hasUtilityProfiles]);

  const loadProfiles = async () => {
    try {
      const response = await apiService.request({ tp: 'req:profiles:list' });
      setProfiles(response.profiles);
    } catch (err) {
      console.error('Failed to load profiles:', err);
      alert(`Failed to load profiles: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  const orderDebounceRef = useRef(null);
  const pendingOrderRef = useRef(null);
  const persistProfileOrder = useCallback(
    orderedProfiles => {
      pendingOrderRef.current = orderedProfiles.map(p => p.id);
      if (orderDebounceRef.current) {
        clearTimeout(orderDebounceRef.current);
      }
      orderDebounceRef.current = setTimeout(async () => {
        const orderedIds = pendingOrderRef.current;
        if (!orderedIds) return;
        try {
          await apiService.request({ tp: 'req:profiles:reorder', order: orderedIds });
        } catch (e) {}
      }, 300);
    },
    [apiService],
  );

  useEffect(() => {
    return () => {
      if (orderDebounceRef.current) {
        clearTimeout(orderDebounceRef.current);
        if (pendingOrderRef.current) {
          apiService
            .request({ tp: 'req:profiles:reorder', order: pendingOrderRef.current })
            .catch(() => {});
        }
      }
    };
  }, [apiService]);

  const moveProfileUp = useCallback(
    id => {
      setProfiles(prev => {
        const idx = prev.findIndex(p => p.id === id);
        if (idx > 0) {
          const next = [...prev];
          [next[idx - 1], next[idx]] = [next[idx], next[idx - 1]];
          persistProfileOrder(next);
          return next;
        }
        return prev;
      });
    },
    [persistProfileOrder],
  );

  const moveProfileDown = useCallback(
    id => {
      setProfiles(prev => {
        const idx = prev.findIndex(p => p.id === id);
        if (idx !== -1 && idx < prev.length - 1) {
          const next = [...prev];
          [next[idx], next[idx + 1]] = [next[idx + 1], next[idx]];
          persistProfileOrder(next);
          return next;
        }
        return prev;
      });
    },
    [persistProfileOrder],
  );

  const moveProfileToTop = useCallback(
    id => {
      setProfiles(prev => {
        const idx = prev.findIndex(p => p.id === id);
        if (idx > 0) {
          const next = [...prev];
          const [item] = next.splice(idx, 1);
          next.unshift(item);
          persistProfileOrder(next);
          return next;
        }
        return prev;
      });
    },
    [persistProfileOrder],
  );

  const moveProfileToBottom = useCallback(
    id => {
      setProfiles(prev => {
        const idx = prev.findIndex(p => p.id === id);
        if (idx !== -1 && idx < prev.length - 1) {
          const next = [...prev];
          const [item] = next.splice(idx, 1);
          next.push(item);
          persistProfileOrder(next);
          return next;
        }
        return prev;
      });
    },
    [persistProfileOrder],
  );

  useEffect(() => {
    const loadData = async () => {
      if (connected.value) {
        await loadProfiles();
      }
    };
    loadData();
  }, []);

  const onDelete = useCallback(
    async id => {
      setLoading(true);
      try {
        await apiService.request({ tp: 'req:profiles:delete', id });
      } catch (err) {
        console.error('Failed to delete profile:', err);
        alert(`Failed to delete profile: ${err.message}`);
      }
      await loadProfiles();
    },
    [apiService],
  );

  const onFavorite = useCallback(
    async id => {
      setLoading(true);
      try {
        await apiService.request({ tp: 'req:profiles:favorite', id });
      } catch (err) {
        console.error('Failed to favorite profile:', err);
        alert(`Failed to favorite profile: ${err.message}`);
      }
      await loadProfiles();
    },
    [apiService],
  );

  const onUnfavorite = useCallback(
    async id => {
      setLoading(true);
      try {
        await apiService.request({ tp: 'req:profiles:unfavorite', id });
      } catch (err) {
        console.error('Failed to unfavorite profile:', err);
        alert(`Failed to unfavorite profile: ${err.message}`);
      }
      await loadProfiles();
    },
    [apiService],
  );

  const onDuplicate = useCallback(
    async id => {
      setLoading(true);
      const original = profiles.find(p => p.id === id);
      if (original) {
        const copy = { ...original };
        delete copy.id;
        delete copy.selected;
        delete copy.favorite;
        copy.label = `${original.label} Copy`;
        try {
          await apiService.request({ tp: 'req:profiles:save', profile: copy });
        } catch (err) {
          console.error('Failed to duplicate profile:', err);
          alert(`Failed to duplicate profile: ${err.message}`);
        }
      }
      await loadProfiles();
    },
    [apiService, profiles],
  );

  const onExport = useCallback(async () => {
    // Re-fetch the authoritative device list at export time rather than
    // serializing the component's `profiles` state, which can lag the device
    // (initial load in flight, a mid-session reconnect, a save that hasn't
    // round-tripped yet). Exporting stale/partial state would silently
    // under-capture the backup the user is about to rely on across a reformat
    // (PRO-218 P2-3). onUpload already fetches req:profiles:list for the same
    // authoritative-state reason.
    let deviceProfiles;
    try {
      const response = await apiService.request({ tp: 'req:profiles:list' });
      deviceProfiles = response?.profiles ?? [];
    } catch (error) {
      console.error('Failed to fetch profiles for export:', error);
      alert(`Profile export failed: could not read profiles from the device (${error.message}).`);
      return;
    }

    // Keep `id` so re-imported profiles stay addressable on the device.
    // Only strip device-local state (`selected`/`favorite`). See
    // migrationTransfer.js for the SPIFFS->LittleFS migration round-trip.
    const exportedProfiles = buildExportPayload(deviceProfiles);

    // Guard against exporting an empty list: writing an empty profiles.json
    // silently produces a useless backup the user may rely on. Warn and skip
    // the download instead (PRO-218 P3 / P2-3).
    if (exportedProfiles.length === 0) {
      alert('No profiles to export yet. Wait for the profile list to load and try again.');
      return;
    }

    const download = prepareDownload('profiles.json');
    try {
      downloadJson(exportedProfiles, 'profiles.json', download);
    } catch (error) {
      download.fail(error);
      console.error('Failed to export profiles:', error);
      alert(`Profile export failed: ${error.message}`);
    }
  }, [apiService]);

  const completeProfileSelect = useCallback(
    async profile => {
      if (!profile) return;

      setLoading(true);
      try {
        await apiService.request({ tp: 'req:profiles:select', id: profile.id });
      } catch (err) {
        console.error('Failed to select profile:', err);
        alert(`Failed to select profile: ${err.message}`);
      }

      await loadProfiles();
    },
    [apiService],
  );

  const onSelect = useCallback(
    async id => {
      const profile = profiles.find(entry => entry.id === id);
      if (!profile) return;

      await completeProfileSelect(profile);
    },
    [profiles, completeProfileSelect],
  );

  // Adapters wiring the shared importProfiles() orchestrator (migrationTransfer.js)
  // onto the real WebSocket. The SAME importProfiles function is exercised by the
  // round-trip tests via a device simulator, so green tests prove the shipped
  // restore algorithm rather than a hand-copied duplicate (PRO-218 P1-2).
  //
  // The id-collision / filename-stem contract and the "saveProfile must echo the
  // stored id" requirement are documented in migrationTransfer.js (PRO-218 P3-4 —
  // single pointer, no restatement, to avoid drift).
  const importAdapters = useMemo(
    () => ({
      listProfiles: async () => {
        const response = await apiService.request({ tp: 'req:profiles:list' });
        return response?.profiles ?? [];
      },
      // request() rejects on a built-in 5s timeout and immediately on socket
      // close, so a hung restore surfaces as a per-item failure naming the
      // profile rather than hanging forever (PRO-218 P1-4).
      saveProfile: async profile => {
        const response = await apiService.request({ tp: 'req:profiles:save', profile });
        return response?.profile ?? {};
      },
    }),
    [apiService],
  );

  // Run the shared restore orchestrator over a parsed batch, surface a whole-picture
  // summary (P2-A: profiles AND files dropped, in one final message), and offer to
  // retry only the failed subset (PRO-218 P1-1).
  //
  // The interaction loop (abort/confirm/proceed/retry/give-up) lives in the tested
  // runRestore() seam in migrationTransfer.js (PRO-218 P2-B). This wrapper only wires
  // the React side: loading state, window.alert/confirm, and a between-retries device
  // refetch. The retry cap (MAX_RESTORE_RETRIES) is enforced inside runRestore so a
  // hung transport can't trap the user in an endless confirm() cycle (NEW-4/NEW-6).
  const restoreProfiles = useCallback(
    async (importedProfiles, fileContext) => {
      setLoading(true);
      try {
        await runRestore({
          adapters: importAdapters,
          importedProfiles,
          fileContext,
          alert: message => alert(message),
          confirm: message => confirm(message),
          onBeforeRetry: () => loadProfiles(),
        });
      } finally {
        await loadProfiles();
      }
    },
    [importAdapters],
  );

  const onUpload = useCallback(
    async evt => {
      const files = Array.from(evt.target.files ?? []);
      if (files.length === 0) return;

      // A backup may have been saved as several per-profile files; restore ALL of
      // them, not just files[0] (PRO-218 P1-3). Read each file, then aggregate
      // through the tested aggregateImportFiles() seam, which preserves the
      // per-FILE outcome so a corrupt/unreadable file in a multi-select can never
      // be silently dropped (PRO-218 NEW-1/NEW-2).
      const fileTexts = await Promise.all(
        files.map(
          file =>
            new Promise(resolve => {
              const reader = new FileReader();
              reader.onload = e =>
                resolve({
                  name: file.name,
                  text: typeof e.target.result === 'string' ? e.target.result : null,
                });
              reader.onerror = () => resolve({ name: file.name, text: null });
              reader.readAsText(file);
            }),
        ),
      );

      const aggregate = aggregateImportFiles(fileTexts);

      // Every selected file that could not be read or parsed to zero profiles is
      // reported BY NAME — never silently filtered out (PRO-218 NEW-1). The count
      // is measured against files SELECTED, not profiles parsed, so a dropped file
      // is always visible.
      if (aggregate.failedFiles.length > 0) {
        if (aggregate.profiles.length === 0) {
          // Nothing parsed at all: this is the corrupt-file abort. Surface the
          // distinct corrupt-file error and ABORT before loadProfiles() repaints
          // the Default-only list (PRO-218 P0-1).
          alert(
            `${formatRestoreSummary({ total: 0, savedCount: 0, failed: [] })}\n\n${formatFileAggregateWarning(aggregate)}`,
          );
          evt.target.value = '';
          return;
        }
        // Some files were good, some failed. Report the failed ones prominently
        // and let the user decide whether to import the survivors or abort to
        // re-check the failed backups first.
        const proceed = confirm(
          `${formatFileAggregateWarning(aggregate)}\n\n` +
            `Import the ${aggregate.profiles.length} profile(s) from the ${aggregate.okCount} ` +
            `readable file(s) anyway? Cancel to re-check the failed file(s) first.`,
        );
        if (!proceed) {
          evt.target.value = '';
          return;
        }
      }

      // Pass the per-FILE outcome into the restore so the FINAL summary reflects
      // the whole picture — profiles restored AND any files that were dropped — in
      // one message, rather than reading as an unqualified "Restored N of N" over
      // just the survivors (PRO-218 P2-A).
      await restoreProfiles(aggregate.profiles, {
        selectedFiles: aggregate.selectedCount,
        okFiles: aggregate.okCount,
        failedFiles: aggregate.failedFiles,
      });
      // Reset the input so re-selecting the same file fires onChange again.
      evt.target.value = '';
    },
    [restoreProfiles],
  );

  const onClear = useCallback(async () => {
    setLoading(true);
    // Per-item try/catch so one failed delete doesn't strand the rest with no
    // feedback (PRO-218 P2-5, same non-atomic pattern as the restore loop). Each
    // failure is collected and surfaced by label; the loop continues.
    const failed = [];
    let deleted = 0;
    const toDelete = profiles.filter(p => !p.selected);
    for (const p of toDelete) {
      try {
        await apiService.request({ tp: 'req:profiles:delete', id: p.id });
        deleted += 1;
      } catch (err) {
        console.error('Failed to delete profile:', p.label || p.id, err);
        failed.push(p.label || p.id || 'unknown');
      }
    }
    if (failed.length > 0) {
      alert(`Deleted ${deleted} of ${toDelete.length} profiles — failed: ${failed.join(', ')}.`);
    }
    await loadProfiles();
  }, [profiles, apiService]);

  const profilesToShow = useMemo(() => {
    if (searchTerm.trim()) {
      const search = searchTerm.toLowerCase().trim();
      return profiles.filter(
        profile =>
          profile.label?.toLowerCase().includes(search) ||
          profile.description?.toLowerCase().includes(search),
      );
    }
    return profiles;
  }, [profiles, searchTerm]);

  if (loading) {
    return (
      <div className='flex w-full flex-row items-center justify-center py-16'>
        <Spinner size={8} />
      </div>
    );
  }

  return (
    <div className='flex flex-col gap-6'>
      {/* Header */}
      <div className='flex items-center justify-between gap-4'>
        <h1 className='font-nd-mono text-[20px] uppercase tracking-[0.08em] text-[var(--text-secondary,#999)]'>
          Profiles
        </h1>
        <div className='flex items-center gap-2'>
          <button
            onClick={onExport}
            className='nd-action-btn'
            disabled={profiles.length === 0}
            title='Export Profiles'
            aria-label='Export Profiles'
          >
            <FontAwesomeIcon icon={faFileExport} />
          </button>
          <ImportButton onChange={onUpload} title='Import Profiles' multiple />
          <ConfirmButton
            onAction={onClear}
            icon={faTrashCan}
            tooltip='Delete all profiles'
            confirmTooltip='Confirm deletion'
          />
        </div>
      </div>

      {/* Search + tabs */}
      <Card sm={12} title='Profiles'>
        <div className='flex flex-col gap-4 mb-5'>
          {/* Search */}
          <div className='flex'>
            <input
              type='text'
              placeholder='Search profiles...'
              value={searchTerm}
              onChange={e => setSearchTerm(e.target.value)}
              className='nd-input flex-1 rounded-r-none border-r-0'
            />
            <span className='nd-input-unit'>
              <FontAwesomeIcon icon={faSearch} className='text-[var(--text-disabled,#666)]' />
            </span>
          </div>

          {/* Tabs */}
          {hasUtilityProfiles && (
            <div className='nd-segmented'>
              <button
                className={`nd-segmented-btn ${activeTab === 'extraction' ? 'nd-segmented-btn--active' : ''}`}
                onClick={() => setActiveTab('extraction')}
              >
                Extraction
              </button>
              <button
                className={`nd-segmented-btn ${activeTab === 'utility' ? 'nd-segmented-btn--active' : ''}`}
                onClick={() => setActiveTab('utility')}
              >
                Utility
              </button>
            </div>
          )}
        </div>

        {/* Add profile */}
        <div className='mb-4'>
          <ProfileAddCard />
        </div>

        {/* Profile list */}
        <div className='flex flex-col gap-3' role='list' aria-label='Profile list'>
          {profilesToShow
            .filter(p => (activeTab === 'utility' ? p.utility : !p.utility))
            .map((data, idx, filtered) => (
              <ProfileCard
                key={data.id}
                data={data}
                onDelete={onDelete}
                onSelect={onSelect}
                favoriteDisabled={favoriteDisabled}
                unfavoriteDisabled={unfavoriteDisabled}
                onUnfavorite={onUnfavorite}
                onFavorite={onFavorite}
                onDuplicate={onDuplicate}
                onMoveUp={moveProfileUp}
                onMoveDown={moveProfileDown}
                onMoveToTop={moveProfileToTop}
                onMoveToBottom={moveProfileToBottom}
                isFirst={idx === 0}
                isLast={idx === filtered.length - 1}
              />
            ))}
        </div>
      </Card>
    </div>
  );
}
