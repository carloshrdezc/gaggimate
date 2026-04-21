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
import { parseProfile } from './utils.js';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArrowUp } from '@fortawesome/free-solid-svg-icons/faArrowUp';
import { faArrowDown } from '@fortawesome/free-solid-svg-icons/faArrowDown';
import { faStar } from '@fortawesome/free-solid-svg-icons/faStar';
import { faPen } from '@fortawesome/free-solid-svg-icons/faPen';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faCopy } from '@fortawesome/free-solid-svg-icons/faCopy';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faEllipsisVertical } from '@fortawesome/free-solid-svg-icons/faEllipsisVertical';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';
import { ConfirmButton } from '../../components/ConfirmButton.jsx';
import { Tooltip } from '../../components/Tooltip.jsx';
import { faTemperatureFull } from '@fortawesome/free-solid-svg-icons/faTemperatureFull';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faScaleBalanced } from '@fortawesome/free-solid-svg-icons/faScaleBalanced';
import { faSearch } from '@fortawesome/free-solid-svg-icons/faSearch';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { buildStatisticsProfileHref } from '../Statistics/utils/statisticsRoute.js';
import { BeanSelectionModal } from './BeanSelectionModal.jsx';
import {
  clearCurrentBeanSelection,
  getLastBeanSelectionForProfile,
  listBeans,
  migrateLegacyBeansToDevice,
  recordBeanSelection,
} from '../../utils/beanManager.js';

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

function ProfileCard({ data, onDelete, onSelect, onFavorite, onUnfavorite, onDuplicate, favoriteDisabled, unfavoriteDisabled, onMoveUp, onMoveDown, isFirst, isLast }) {
  const { armed: confirmDelete, armOrRun: confirmOrDelete } = useConfirmAction(4000);
  const bookmarkClass = data.favorite ? 'text-[--warning]' : 'text-[--text-muted]';
  const typeText = data.type === 'pro' ? 'Pro' : 'Simple';

  const onFavoriteToggle = useCallback(() => {
    if (data.favorite && !unfavoriteDisabled) onUnfavorite(data.id);
    else if (!data.favorite && !favoriteDisabled) onFavorite(data.id);
  }, [data.favorite, unfavoriteDisabled, favoriteDisabled, onUnfavorite, onFavorite, data.id]);

  const onDownload = useCallback(() => {
    const { id, selected, favorite, ...profileData } = data;
    const filename = `profile-${data.id}.json`;
    const prepared = prepareDownload(filename);
    if (!prepared.targetWindow) {
      console.error('Failed to create download window - popup blocker may be active');
      alert('Download failed: Please allow popups for this site and try again.');
      return;
    }
    try {
      downloadJson(profileData, filename, prepared);
    } catch (error) {
      prepared.fail(error);
      console.error('Failed to export profile:', error);
      alert(`Profile export failed: ${error.message}`);
    }
  }, [data]);

  const statsHref = buildStatisticsProfileHref({ source: 'gaggimate', profileName: data.label });

  const [detailsCollapsed, setDetailsCollapsed] = useState(true);
  const onToggleDetails = useCallback(() => setDetailsCollapsed(v => !v), []);
  const detailsSectionId = `profile-${data.id}-summary`;

  const totalDurationSeconds = Array.isArray(data?.phases)
    ? data.phases.reduce((sum, p) => sum + (Number.isFinite(p?.duration) ? p.duration : 0), 0)
    : 0;

  // Kebab menu
  const kebabRef = useRef(null);
  const popoverRef = useRef(null);
  const [menuOpen, setMenuOpen] = useState(false);

  const positionPopover = useCallback(() => {
    const btn = kebabRef.current;
    const pop = popoverRef.current;
    if (!btn || !pop) return;
    const rect = btn.getBoundingClientRect();
    if (!pop.matches(':popover-open')) {
      try { pop.showPopover(); } catch (_) {}
    }
    const w = pop.offsetWidth || 224;
    const h = pop.offsetHeight || 0;
    const gap = 6;
    let top = rect.bottom + gap;
    let left = rect.right - w;
    const margin = 8;
    if (left < margin) left = margin;
    const maxLeft = window.innerWidth - w - margin;
    if (left > maxLeft) left = maxLeft;
    const maxTop = window.innerHeight - h - margin;
    if (top > maxTop) top = Math.max(margin, rect.top - h - gap);
    pop.style.position = 'fixed';
    pop.style.inset = 'auto auto auto auto';
    pop.style.left = `${left}px`;
    pop.style.top = `${top}px`;
  }, []);

  const closeMenu = useCallback(() => {
    const pop = popoverRef.current;
    if (pop && pop.matches(':popover-open')) {
      try { pop.hidePopover(); } catch (_) {}
    }
    setMenuOpen(false);
  }, []);

  const toggleMenu = useCallback(e => {
    e?.preventDefault?.();
    const pop = popoverRef.current;
    if (!pop) return;
    if (pop.matches(':popover-open')) {
      closeMenu();
    } else {
      positionPopover();
      try { pop.showPopover(); setMenuOpen(true); } catch (_) {}
    }
  }, [closeMenu, positionPopover]);

  useEffect(() => {
    const pop = popoverRef.current;
    if (!pop) return;
    const onToggle = () => {
      const isOpen = pop.matches(':popover-open');
      setMenuOpen(isOpen);
      if (isOpen) positionPopover();
    };
    const onResize = () => { if (pop.matches(':popover-open')) positionPopover(); };
    pop.addEventListener('toggle', onToggle);
    window.addEventListener('resize', onResize);
    window.addEventListener('scroll', onResize, true);
    return () => {
      pop.removeEventListener('toggle', onToggle);
      window.removeEventListener('resize', onResize);
      window.removeEventListener('scroll', onResize, true);
    };
  }, [positionPopover]);

  // Sum duration
  const lastVolumetricTarget = data.phases?.length > 0
    ? data.phases.at(-1)?.targets?.find(t => t.type === 'volumetric')?.value
    : null;

  return (
    <div class="group relative rounded-xl p-4 transition-all duration-150 hover:bg-[--bg-elevated] hover:border-[--border-active]" style="border: 1px solid transparent;">
      {/* Main row */}
      <div class="flex flex-row items-center gap-4">
        {/* Checkbox */}
        <div>
          <label class="cursor-pointer">
            <input
              checked={data.selected}
              type="checkbox"
              onClick={() => onSelect(data.id)}
              class="checkbox checkbox-success checkbox-sm"
              aria-label={`Select ${data.label} profile`}
            />
          </label>
        </div>

        {/* Label + info */}
        <div class="flex min-w-0 flex-grow flex-col gap-1">
          <div class="flex flex-row items-center gap-3 flex-wrap">
            <span id={`profile-${data.id}-title`} class="text-base font-semibold text-[--text-primary] truncate">
              {data.label}
            </span>
            <span class="text-xs text-[--text-muted] font-data px-2 py-0.5 rounded-full" style="background: rgba(255,255,255,0.05);">
              {typeText}
            </span>
            {data.favorite && (
              <FontAwesomeIcon icon={faStar} class="text-[--warning] text-sm" />
            )}
            <button
              onClick={onToggleDetails}
              class="btn btn-xs btn-ghost text-[--text-muted] hover:text-[--text-primary]"
              aria-label={`${detailsCollapsed ? 'Show' : 'Hide'} details`}
              aria-expanded={!detailsCollapsed}
              aria-controls={detailsSectionId}
            >
              <FontAwesomeIcon icon={faChevronRight} class={`transition-transform ${!detailsCollapsed ? 'rotate-90' : ''}`} />
            </button>
          </div>

          {/* Inline stats */}
          <div class="flex flex-row gap-3 text-xs text-[--text-muted]">
            <span class="font-data">{data.temperature}°C</span>
            <span>·</span>
            <span class="font-data">{totalDurationSeconds}s</span>
            {lastVolumetricTarget && (
              <>
                <span>·</span>
                <span class="font-data">{lastVolumetricTarget}g</span>
              </>
            )}
            <span>·</span>
            <span>{data.phases?.length || 0} phases</span>
          </div>
        </div>

        {/* Actions - desktop: reveal on hover */}
        <div class="hidden flex-row items-center gap-1 sm:flex">
          <button
            onClick={onFavoriteToggle}
            disabled={data.favorite ? unfavoriteDisabled : favoriteDisabled}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--warning] transition-all duration-150"
            aria-label={data.favorite ? 'Remove from favorites' : 'Add to favorites'}
          >
            <FontAwesomeIcon icon={faStar} />
          </button>
          <a
            href={`/profiles/${data.id}`}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--text-primary] transition-all duration-150"
            aria-label="Edit profile"
          >
            <FontAwesomeIcon icon={faPen} />
          </a>
          <a
            href={statsHref}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--success] transition-all duration-150"
            aria-label="View statistics"
          >
            <FontAwesomeIcon icon={faChartSimple} />
          </a>
          <button
            onClick={onDownload}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--accent] transition-all duration-150"
            aria-label="Export profile"
          >
            <FontAwesomeIcon icon={faFileExport} />
          </button>
          <button
            onClick={() => onDuplicate(data.id)}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--text-primary] transition-all duration-150"
            aria-label="Duplicate profile"
          >
            <FontAwesomeIcon icon={faCopy} />
          </button>
          <button
            onClick={() => confirmOrDelete(() => onDelete(data.id))}
            class={`btn btn-sm btn-ghost transition-all duration-150 ${confirmDelete ? 'bg-[--error] text-white' : 'text-[--error] hover:bg-[--error]/10'}`}
            aria-label={confirmDelete ? 'Click to confirm delete' : 'Delete profile'}
          >
            <FontAwesomeIcon icon={faTrashCan} />
          </button>
        </div>

        {/* Mobile: kebab menu */}
        <div class="sm:hidden">
          <button
            ref={kebabRef}
            onClick={toggleMenu}
            class="btn btn-sm btn-ghost text-[--text-muted]"
            aria-label="Open actions menu"
            aria-haspopup="menu"
            aria-expanded={menuOpen}
          >
            <FontAwesomeIcon icon={faEllipsisVertical} />
          </button>
          <div
            ref={popoverRef}
            popover="auto"
            role="menu"
            class="bg-[--bg-elevated] rounded-xl p-2 shadow-lg"
            style="border: 1px solid var(--border);"
            onKeyDown={e => { if (e.key === 'Escape') closeMenu(); }}
          >
            <ul class="space-y-1">
              <li>
                <button
                  role="menuitem"
                  onClick={() => { onFavoriteToggle(); closeMenu(); }}
                  class="w-full text-left px-3 py-2 rounded-lg text-sm text-[--text-secondary] hover:bg-[--bg-glass] hover:text-[--text-primary] transition-all"
                >
                  <FontAwesomeIcon icon={faStar} class={bookmarkClass} />
                  <span class="ml-2">{data.favorite ? 'Unfavorite' : 'Favorite'}</span>
                </button>
              </li>
              <li>
                <a
                  role="menuitem"
                  href={`/profiles/${data.id}`}
                  onClick={closeMenu}
                  class="block px-3 py-2 rounded-lg text-sm text-[--text-secondary] hover:bg-[--bg-glass] hover:text-[--text-primary] transition-all"
                >
                  <FontAwesomeIcon icon={faPen} />
                  <span class="ml-2">Edit</span>
                </a>
              </li>
              <li>
                <a
                  role="menuitem"
                  href={statsHref}
                  onClick={closeMenu}
                  class="block px-3 py-2 rounded-lg text-sm text-[--success] hover:bg-[--bg-glass] transition-all"
                >
                  <FontAwesomeIcon icon={faChartSimple} />
                  <span class="ml-2">Statistics</span>
                </a>
              </li>
              <li>
                <button
                  role="menuitem"
                  onClick={() => { onDownload(); closeMenu(); }}
                  class="w-full text-left px-3 py-2 rounded-lg text-sm text-[--accent] hover:bg-[--bg-glass] transition-all"
                >
                  <FontAwesomeIcon icon={faFileExport} />
                  <span class="ml-2">Export</span>
                </button>
              </li>
              <li>
                <button
                  role="menuitem"
                  onClick={() => { onDuplicate(data.id); closeMenu(); }}
                  class="w-full text-left px-3 py-2 rounded-lg text-sm text-[--text-secondary] hover:bg-[--bg-glass] hover:text-[--text-primary] transition-all"
                >
                  <FontAwesomeIcon icon={faCopy} />
                  <span class="ml-2">Duplicate</span>
                </button>
              </li>
              <li>
                <button
                  role="menuitem"
                  onClick={() => { confirmOrDelete(() => { onDelete(data.id); closeMenu(); }); }}
                  class={`w-full text-left px-3 py-2 rounded-lg text-sm transition-all ${confirmDelete ? 'bg-[--error] text-white font-semibold' : 'text-[--error] hover:bg-[--bg-glass]'}`}
                >
                  <FontAwesomeIcon icon={faTrashCan} />
                  <span class="ml-2">{confirmDelete ? 'Confirm Delete' : 'Delete'}</span>
                </button>
              </li>
            </ul>
          </div>
        </div>

        {/* Reorder buttons */}
        <div class="hidden flex-col gap-0 sm:flex">
          <button
            onClick={() => onMoveUp(data.id)}
            disabled={isFirst}
            class="btn btn-xs btn-ghost text-[--text-muted] hover:text-[--text-primary] rounded-b-none"
            aria-label="Move up"
          >
            <FontAwesomeIcon icon={faArrowUp} />
          </button>
          <button
            onClick={() => onMoveDown(data.id)}
            disabled={isLast}
            class="btn btn-xs btn-ghost text-[--text-muted] hover:text-[--text-primary] rounded-t-none"
            aria-label="Move down"
          >
            <FontAwesomeIcon icon={faArrowDown} />
          </button>
        </div>
      </div>

      {/* Expanded details */}
      {!detailsCollapsed && (
        <div id={detailsSectionId} class="mt-4 flex flex-col gap-3 pl-9">
          {data.description && (
            <p class="text-sm text-[--text-secondary]">{data.description}</p>
          )}
          <div class="flex flex-row gap-2 flex-wrap">
            <span class="text-xs text-[--text-muted] px-2 py-1 rounded-lg" style="background: rgba(255,255,255,0.05);">
              <FontAwesomeIcon icon={faTemperatureFull} class="mr-1" />
              <span class="font-data">{data.temperature}°C</span>
            </span>
            <span class="text-xs text-[--text-muted] px-2 py-1 rounded-lg" style="background: rgba(255,255,255,0.05);">
              <FontAwesomeIcon icon={faClock} class="mr-1" />
              <span class="font-data">{totalDurationSeconds}s</span>
            </span>
            {lastVolumetricTarget && (
              <span class="text-xs text-[--text-muted] px-2 py-1 rounded-lg" style="background: rgba(255,255,255,0.05);">
                <FontAwesomeIcon icon={faScaleBalanced} class="mr-1" />
                <span class="font-data">{lastVolumetricTarget}g</span>
              </span>
            )}
          </div>
          {/* Chart area */}
          <div class="mt-2 overflow-x-auto">
            {data.type === 'pro' ? (
              <ExtendedProfileChart data={data} class="max-h-36" />
            ) : (
              <SimpleContent data={data} />
            )}
          </div>
        </div>
      )}
    </div>
  );
}

function SimpleContent({ data }) {
  return (
    <div class="flex flex-row items-center gap-2" role="list">
      {data.phases.map((phase, i) => (
        <div key={i} class="flex flex-row items-center gap-2" role="listitem">
          {i > 0 && <FontAwesomeIcon icon={faChevronRight} class="text-[--text-muted]" aria-hidden="true" />}
          <SimpleStep phase={phase.phase} type={phase.name} duration={phase.duration} targets={phase.targets || []} />
        </div>
      ))}
    </div>
  );
}

function SimpleStep({ phase, type, duration, targets }) {
  return (
    <div class="rounded-lg p-3" style="background: rgba(255,255,255,0.03); border: 1px solid var(--border);">
      <div class="flex flex-row gap-2">
        <span class="text-sm font-medium text-[--text-primary]">{PhaseLabels[phase]}</span>
        <span class="text-sm text-[--text-muted]">{type}</span>
      </div>
      <div class="text-xs text-[--text-muted] mt-1">
        {targets.length === 0 && <span>Duration: <span class="font-data">{duration}s</span></span>}
        {targets.map((t, i) => (
          <span key={i}>Exit on: <span class="font-data">{t.value}{t.type === 'volumetric' ? 'g' : ''}</span></span>
        ))}
      </div>
    </div>
  );
}

export function ProfileList() {
  const apiService = useContext(ApiServiceContext);
  const [profiles, setProfiles] = useState([]);
  const [beans, setBeans] = useState([]);
  const [beanSelectionProfile, setBeanSelectionProfile] = useState(null);
  const [selectedBeanId, setSelectedBeanId] = useState('');
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

  useEffect(() => {
    let cancelled = false;
    const loadBeans = async () => {
      try {
        await migrateLegacyBeansToDevice(apiService);
        const loadedBeans = await listBeans(apiService);
        if (!cancelled) {
          setBeans(loadedBeans.filter(bean => !bean.archived));
        }
      } catch (error) {
        console.error('Failed to load beans:', error);
      }
    };
    loadBeans();
    const handleBeansChanged = () => loadBeans();
    window.addEventListener('beans-library-changed', handleBeansChanged);
    return () => {
      cancelled = true;
      window.removeEventListener('beans-library-changed', handleBeansChanged);
    };
  }, [apiService]);

  useEffect(() => {
    loadProfiles();
    const handler = () => loadProfiles();
    const id = apiService.on('manual:saved', handler);
    return () => apiService.off('manual:saved', id);
  }, [apiService]);

  const loadProfiles = async () => {
    const response = await apiService.request({ tp: 'req:profiles:list' });
    setProfiles(response.profiles);
    setLoading(false);
  };

  const orderDebounceRef = useRef(null);
  const pendingOrderRef = useRef(null);
  const persistProfileOrder = useCallback(orderedProfiles => {
    pendingOrderRef.current = orderedProfiles.map(p => p.id);
    if (orderDebounceRef.current) clearTimeout(orderDebounceRef.current);
    orderDebounceRef.current = setTimeout(async () => {
      const orderedIds = pendingOrderRef.current;
      if (!orderedIds) return;
      try {
        await apiService.request({ tp: 'req:profiles:reorder', order: orderedIds });
      } catch (e) {}
    }, 300);
  }, [apiService]);

  useEffect(() => {
    return () => {
      if (orderDebounceRef.current) {
        clearTimeout(orderDebounceRef.current);
        if (pendingOrderRef.current) {
          apiService.request({ tp: 'req:profiles:reorder', order: pendingOrderRef.current }).catch(() => {});
        }
      }
    };
  }, [apiService]);

  const moveProfileUp = useCallback(id => {
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
  }, [persistProfileOrder]);

  const moveProfileDown = useCallback(id => {
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
  }, [persistProfileOrder]);

  useEffect(() => {
    const loadData = async () => {
      if (connected.value) await loadProfiles();
    };
    loadData();
  }, [connected.value]);

  const onDelete = useCallback(async id => {
    setLoading(true);
    await apiService.request({ tp: 'req:profiles:delete', id });
    await loadProfiles();
  }, [apiService, setLoading]);

  const onFavorite = useCallback(async id => {
    setLoading(true);
    await apiService.request({ tp: 'req:profiles:favorite', id });
    await loadProfiles();
  }, [apiService, setLoading]);

  const onUnfavorite = useCallback(async id => {
    setLoading(true);
    await apiService.request({ tp: 'req:profiles:unfavorite', id });
    await loadProfiles();
  }, [apiService, setLoading]);

  const onDuplicate = useCallback(async id => {
    setLoading(true);
    const original = profiles.find(p => p.id === id);
    if (original) {
      const copy = { ...original };
      delete copy.id;
      delete copy.selected;
      delete copy.favorite;
      copy.label = `${original.label} Copy`;
      await apiService.request({ tp: 'req:profiles:save', profile: copy });
    }
    await loadProfiles();
  }, [apiService, profiles, setLoading]);

  const onExport = useCallback(() => {
    const exportedProfiles = profiles.map(p => {
      const ep = { ...p };
      delete ep.id;
      delete ep.selected;
      delete ep.favorite;
      return ep;
    });
    const download = prepareDownload('profiles.json');
    try {
      downloadJson(exportedProfiles, 'profiles.json', download);
    } catch (error) {
      download.fail(error);
      console.error('Failed to export profiles:', error);
      alert(`Profile export failed: ${error.message}`);
    }
  }, [profiles]);

  const completeProfileSelect = useCallback(async (profile, beanId = '') => {
    if (!profile) return;
    setLoading(true);
    await apiService.request({ tp: 'req:profiles:select', id: profile.id });
    let selectedBeanName = '';
    if (beanId) {
      const selectedBean = (await listBeans(apiService)).find(bean => bean.id === beanId);
      if (selectedBean) {
        selectedBeanName = selectedBean.name;
        recordBeanSelection({ profileId: profile.id, profileLabel: profile.label, bean: selectedBean });
      }
    } else {
      clearCurrentBeanSelection();
    }
    apiService.send({ tp: 'req:beans:select', name: selectedBeanName });
    await loadProfiles();
    setBeanSelectionProfile(null);
    setSelectedBeanId('');
  }, [apiService]);

  const onSelect = useCallback(async id => {
    const profile = profiles.find(entry => entry.id === id);
    if (!profile) return;
    const availableBeans = (await listBeans(apiService)).filter(bean => !bean.archived);
    setBeans(availableBeans);
    if (availableBeans.length === 0) {
      await completeProfileSelect(profile);
      return;
    }
    const lastBeanSelection = getLastBeanSelectionForProfile(profile);
    setSelectedBeanId(lastBeanSelection?.beanId || availableBeans[0]?.id || '');
    setBeanSelectionProfile(profile);
  }, [apiService, profiles, completeProfileSelect]);

  const onUpload = function (evt) {
    if (evt.target.files.length) {
      const file = evt.target.files[0];
      const reader = new FileReader();
      reader.onload = async e => {
        const result = e.target.result;
        if (typeof result === 'string') {
          setLoading(true);
          try {
            const profiles = parseProfile(result);
            for (const p of profiles) {
              await apiService.request({ tp: 'req:profiles:save', profile: p });
            }
          } catch {}
          await loadProfiles();
        }
      };
      reader.readAsText(file);
    }
  };

  const onClear = useCallback(async () => {
    setLoading(true);
    for (const p of profiles) {
      if (!p.selected) {
        await apiService.request({ tp: 'req:profiles:delete', id: p.id });
      }
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

  // Loading skeleton
  if (loading) {
    return (
      <div class="space-y-4">
        <div class="h-8 w-32 rounded-lg skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        <div class="space-y-3">
          {[1, 2, 3].map(i => (
            <div key={i} class="h-20 rounded-xl skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
          ))}
        </div>
      </div>
    );
  }

  const filteredProfiles = profilesToShow.filter(p => (activeTab === 'utility' ? p.utility : !p.utility));

  return (
    <div class="space-y-6">
      {/* Page Header */}
      <div class="flex flex-row items-center justify-between">
        <h1 class="text-2xl font-semibold text-[--text-primary]">Profiles</h1>
        <div class="flex flex-row gap-2">
          <button
            onClick={onExport}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all duration-150"
            disabled={profiles.length === 0}
          >
            <FontAwesomeIcon icon={faFileExport} />
            <span>Export</span>
          </button>
          <label
            htmlFor="profileImport"
            class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium cursor-pointer text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all duration-150"
          >
            <FontAwesomeIcon icon={faFileImport} />
            <span>Import</span>
          </label>
          <input onChange={onUpload} class="hidden" id="profileImport" type="file" accept=".json,application/json" />
          <ConfirmButton
            onAction={onClear}
            icon={faTrashCan}
            tooltip="Delete all profiles"
            confirmTooltip="Confirm deletion"
          />
        </div>
      </div>

      {/* Search */}
      <div class="relative">
        <FontAwesomeIcon icon={faSearch} class="absolute left-4 top-1/2 -translate-y-1/2 text-[--text-muted]" />
        <input
          type="text"
          placeholder="Search profiles..."
          value={searchTerm}
          onChange={e => setSearchTerm(e.target.value)}
          class="w-full pl-10 pr-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
          style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
          onFocus={e => e.target.style.borderColor = 'var(--accent)'}
          onBlur={e => e.target.style.borderColor = 'var(--border)'}
        />
      </div>

      {/* Add new profile */}
      <div>
        <ProfileAddCard />
      </div>

      {/* Tabs */}
      {hasUtilityProfiles && (
        <div class="flex flex-row gap-1 border-b" style="border-color: var(--border);">
          <button
            onClick={() => setActiveTab('extraction')}
            class="px-4 py-2 text-sm font-medium transition-all duration-150"
            style={activeTab === 'extraction' ? 'color: var(--accent); border-bottom: 2px solid var(--accent);' : 'color: var(--text-secondary); border-bottom: 2px solid transparent;'}
          >
            Extraction
          </button>
          <button
            onClick={() => setActiveTab('utility')}
            class="px-4 py-2 text-sm font-medium transition-all duration-150"
            style={activeTab === 'utility' ? 'color: var(--accent); border-bottom: 2px solid var(--accent);' : 'color: var(--text-secondary); border-bottom: 2px solid transparent;'}
          >
            Utility
          </button>
        </div>
      )}

      {/* Profile list */}
      <div class="space-y-2" role="list">
        {filteredProfiles.length === 0 ? (
          <div class="flex flex-col items-center justify-center py-16 text-center">
            <div class="mb-4 text-4xl opacity-20">
              <FontAwesomeIcon icon={faChartSimple} />
            </div>
            <p class="text-[--text-muted] mb-4">No profiles found.</p>
            <a
              href="/profiles/new"
              class="inline-flex items-center gap-2 px-5 py-2.5 rounded-lg text-sm font-medium text-[--bg-base] transition-all hover:opacity-90"
              style="background: var(--accent);"
            >
              <FontAwesomeIcon icon={faPlus} />
              <span>Create Profile</span>
            </a>
          </div>
        ) : (
          filteredProfiles.map((data, idx, filtered) => (
            <div
              key={data.id}
              style={{ animationDelay: `${idx * 50}ms` }}
            >
              <ProfileCard
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
                isFirst={idx === 0}
                isLast={idx === filtered.length - 1}
              />
            </div>
          ))
        )}
      </div>

      <BeanSelectionModal
        open={!!beanSelectionProfile}
        profile={beanSelectionProfile}
        beans={beans}
        selectedBeanId={selectedBeanId}
        onBeanChange={setSelectedBeanId}
        onClose={() => { setBeanSelectionProfile(null); setSelectedBeanId(''); }}
        onSkip={() => completeProfileSelect(beanSelectionProfile)}
        onConfirm={() => completeProfileSelect(beanSelectionProfile, selectedBeanId)}
      />
    </div>
  );
}