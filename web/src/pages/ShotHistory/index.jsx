import {
  Chart,
  LineController,
  TimeScale,
  LinearScale,
  PointElement,
  LineElement,
  Legend,
  Filler,
  CategoryScale,
} from 'chart.js';
import 'chartjs-adapter-dayjs-4/dist/chartjs-adapter-dayjs-4.esm';
Chart.register(LineController);
Chart.register(TimeScale);
Chart.register(LinearScale);
Chart.register(CategoryScale);
Chart.register(PointElement);
Chart.register(LineElement);
Chart.register(Filler);
Chart.register(Legend);

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { useCallback, useEffect, useRef, useState, useContext, useMemo } from 'preact/hooks';
import { computed } from '@preact/signals';
import { Spinner } from '../../components/Spinner.jsx';
import HistoryCard from './HistoryCard.jsx';
import Card from '../../components/Card.jsx';
import { parseBinaryShot } from './parseBinaryShot.js';
import { parseBinaryIndex, indexToShotList } from './parseBinaryIndex.js';
import { indexedDBService } from '../ShotAnalyzer/services/IndexedDBService.js';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { buildShotHistoryArchive, importShotHistoryArchive } from './historyArchive.js';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSearch } from '@fortawesome/free-solid-svg-icons/faSearch';
import { faSort } from '@fortawesome/free-solid-svg-icons/faSort';
import { faFilter } from '@fortawesome/free-solid-svg-icons/faFilter';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faChevronLeft } from '@fortawesome/free-solid-svg-icons/faChevronLeft';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';
import { faXmark } from '@fortawesome/free-solid-svg-icons/faXmark';
import { inferBeanForShot, inferBeanIdForShot, isBeanRecordedForShot, listBeans } from '../../utils/beanManager.js';
import { inferGrinderForShot, inferGrindSettingForShot, isGrinderRecordedForShot } from '../../utils/grinderManager.js';
import {
  defaultFilters,
  hasActiveFilters,
  applyShotFilters,
  availableProfiles,
  availableBeans,
  filtersFromQuery,
  mergeFiltersIntoSearch,
} from './shotFilters.js';

const connected = computed(() => machine.value.connected);

function getHistoryKey(shot) {
  return `${shot.source || 'gaggimate'}:${shot.id}`;
}

function normalizeBrowserShot(shot) {
  return {
    ...shot,
    source: 'browser',
    loaded: Array.isArray(shot.samples) && shot.samples.length > 0,
  };
}

export function ShotHistory() {
  const apiService = useContext(ApiServiceContext);
  const importInputRef = useRef(null);
  const loadHistoryAbortRef = useRef(null);
  const [history, setHistory] = useState([]);
  const [loading, setLoading] = useState(true);
  const [archiveBusy, setArchiveBusy] = useState(false);
  const [searchTerm, setSearchTerm] = useState('');
  const [sortBy, setSortBy] = useState('date');
  const [sortOrder, setSortOrder] = useState('desc');
  const [filterBy, setFilterBy] = useState('all');
  const [currentPage, setCurrentPage] = useState(1);
  const itemsPerPage = 10;

  const [allBeans, setAllBeans] = useState([]);

  // Advanced filters (PRO-31). Initialized from the URL query params so applied
  // filters persist across navigation (read on mount, written on change).
  const [filters, setFilters] = useState(() =>
    typeof window !== 'undefined' ? filtersFromQuery(window.location.search) : defaultFilters(),
  );
  const [filtersOpen, setFiltersOpen] = useState(false);

  // Update a single filter field and reset pagination.
  const updateFilter = useCallback((key, value) => {
    setFilters(prev => ({ ...prev, [key]: value }));
    setCurrentPage(1);
  }, []);

  const clearFilters = useCallback(() => {
    setFilters(defaultFilters());
    setCurrentPage(1);
  }, []);

  // Persist filters to the URL query string via replaceState (so filter changes
  // do not stack history entries and survive navigating away and back).
  useEffect(() => {
    if (typeof window === 'undefined') return;
    const query = mergeFiltersIntoSearch(window.location.search, filters);
    const next = `${window.location.pathname}${query}${window.location.hash}`;
    const current = `${window.location.pathname}${window.location.search}${window.location.hash}`;
    if (next !== current) {
      window.history.replaceState(window.history.state, '', next);
    }
  }, [filters]);

  useEffect(() => {
    let cancelled = false;
    const load = () => {
      listBeans(apiService)
        .then(beans => { if (!cancelled) setAllBeans(beans); })
        .catch(() => {});
    };
    load();
    window.addEventListener('beans-library-changed', load);
    return () => {
      cancelled = true;
      window.removeEventListener('beans-library-changed', load);
    };
  }, [apiService, connected.value]);

  const enrichShotWithBean = useCallback(
    shot => ({
      ...shot,
      beanName: inferBeanForShot(shot),
      beanId: inferBeanIdForShot(shot),
      beanRecorded: isBeanRecordedForShot(shot),
      // PRO-434 (ref PRO-430 / PR #429): reserved for symmetry with `beanRecorded`.
      // No current consumer — unlike beans, grinders have no archived lifecycle,
      // so there is no `grinderArchived` feature to gate. Seeds a future one.
      grinderRecorded: isGrinderRecordedForShot(shot),
      // PRO-425: pre-fill defaults for Shot Notes' grinder / grindSetting from
      // the dashboard grinder-selection log. Editable — not authoritative.
      grinder: inferGrinderForShot(shot),
      grindSetting: inferGrindSettingForShot(shot),
    }),
    [],
  );

  useEffect(() => {
    notesService.setApiService(apiService);
  }, [apiService]);

  const mergeShots = useCallback(
    (deviceShots, browserShots) => {
      setHistory(prev => {
        const existingMap = new Map(prev.map(shot => [getHistoryKey(shot), shot]));
        const nextShots = [...deviceShots, ...browserShots].map(shot => {
          const key = getHistoryKey(shot);
          const existing = existingMap.get(key);

          if (shot.source === 'gaggimate' && existing?.loaded) {
            return enrichShotWithBean({
              ...existing,
              ...shot,
              rating: existing.rating ?? shot.rating,
              volume: shot.volume ?? existing.volume,
              incomplete: shot.incomplete ?? existing.incomplete,
              loaded: true,
            });
          }

          if (shot.source === 'browser' && existing) {
            return enrichShotWithBean({
              ...existing,
              ...shot,
              loaded: shot.loaded || existing.loaded,
            });
          }

          return enrichShotWithBean(shot);
        });

        return nextShots.sort((a, b) => b.timestamp - a.timestamp);
      });
    },
    [enrichShotWithBean],
  );

  const loadHistory = useCallback(async () => {
    loadHistoryAbortRef.current?.abort();
    const controller = new AbortController();
    loadHistoryAbortRef.current = controller;
    setLoading(true);

    try {
      const browserShots = (await indexedDBService.getAllShots()).map(normalizeBrowserShot);
      let deviceShots = [];

      if (connected.value) {
        const response = await fetch('/api/history/index.bin', { signal: controller.signal });
        if (response.ok) {
          const arrayBuffer = await response.arrayBuffer();
          const indexData = parseBinaryIndex(arrayBuffer);
          deviceShots = indexToShotList(indexData).map(shot => ({ ...shot, source: 'gaggimate' }));
        } else if (response.status !== 404) {
          throw new Error(`HTTP ${response.status}`);
        }
      }

      mergeShots(deviceShots, browserShots);
    } catch (error) {
      if (error.name === 'AbortError') return;
      console.error('Failed to load shot history:', error);
      setHistory([]);
    } finally {
      setLoading(false);
    }
  }, [mergeShots]);

  useEffect(() => {
    loadHistory();
    return () => loadHistoryAbortRef.current?.abort();
  }, [loadHistory, connected.value]);

  const loadShotDetails = useCallback(async shot => {
    if (!shot) return null;
    if (shot.loaded) return shot;

    try {
      if (shot.source === 'browser') {
        const storageKey = shot.storageKey || shot.name || shot.id;
        const storedShot = await indexedDBService.getShot(storageKey);
        if (!storedShot) return null;

        const loadedShot = enrichShotWithBean({
          ...shot,
          ...storedShot,
          source: 'browser',
          loaded: true,
        });

        setHistory(prev =>
          prev.map(item =>
            getHistoryKey(item) === getHistoryKey(shot)
              ? loadedShot
              : item,
          ),
        );
        return loadedShot;
      }

      const paddedId = String(shot.id).padStart(6, '0');
      const resp = await fetch(`/api/history/${paddedId}.slog`);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const buf = await resp.arrayBuffer();
      const parsed = parseBinaryShot(buf, shot.id);
      parsed.incomplete = (shot?.incomplete ?? false) || parsed.incomplete;
      if (shot?.notes) parsed.notes = shot.notes;

      const loadedShot = enrichShotWithBean({
        ...shot,
        ...parsed,
        volume: shot.volume ?? parsed.volume,
        rating: shot.rating ?? parsed.rating,
        incomplete: shot.incomplete ?? parsed.incomplete,
        source: 'gaggimate',
        loaded: true,
      });

      setHistory(prev =>
        prev.map(item =>
          getHistoryKey(item) === getHistoryKey(shot)
            ? loadedShot
            : item,
        ),
      );
      return loadedShot;
    } catch (e) {
      console.error('Failed loading shot', e);
      throw e;
    }
  }, [enrichShotWithBean]);

  const buildExportShot = useCallback(async shot => {
    if (shot.source === 'browser') {
      const storageKey = shot.storageKey || shot.name || shot.id;
      const storedShot = await indexedDBService.getShot(storageKey);
      const notes = await notesService.loadNotes(storageKey, 'browser');
      return {
        ...(storedShot || shot),
        id: String(shot.id),
        source: 'browser',
        loaded: Array.isArray((storedShot || shot)?.samples) && (storedShot || shot).samples.length > 0,
        notes,
      };
    }

    const notes = await notesService.loadNotes(String(shot.id), 'gaggimate');
    return {
      ...shot,
      id: String(shot.id),
      source: 'gaggimate',
      loaded: Array.isArray(shot.samples) && shot.samples.length > 0,
      volume: shot.volume ?? null,
      rating: notes.rating || shot.rating || 0,
      incomplete: shot.incomplete ?? false,
      notes,
    };
  }, []);

  const onDelete = useCallback(
    async shot => {
      setLoading(true);
      try {
        if (shot.source === 'browser') {
          const storageKey = shot.storageKey || shot.name || shot.id;
          await indexedDBService.deleteShot(storageKey);
          await indexedDBService.deleteNotes(storageKey);
        } else {
          await apiService.request({ tp: 'req:history:delete', id: shot.id });
        }
      } finally {
        await loadHistory();
      }
    },
    [apiService, loadHistory],
  );

  const onNotesChanged = useCallback((id, notes, source) => {
    setHistory(prev =>
      prev.map(shot =>
        shot.id === id && shot.source === source
          ? {
              ...shot,
              notes,
              rating: notes.rating || 0,
            }
          : shot,
      ),
    );
  }, []);

  const handleExportAll = useCallback(async () => {
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    const filename = `shot-history-${stamp}.json`;
    const download = prepareDownload(filename);
    setArchiveBusy(true);
    try {
      const exportShots = [];
      for (const shot of history) {
        exportShots.push(await buildExportShot(shot));
      }
      const archive = buildShotHistoryArchive(exportShots);
      downloadJson(archive, filename, download);
    } catch (error) {
      console.error('Failed to export shot history archive:', error);
      download.fail(error);
      alert(`Export failed: ${error.message}`);
    } finally {
      setArchiveBusy(false);
    }
  }, [buildExportShot, history]);

  const handleImportFile = useCallback(
    async event => {
      const [file] = Array.from(event.target.files || []);
      if (!file) return;

      setArchiveBusy(true);
      try {
        const payload = JSON.parse(await file.text());
        const imported = await importShotHistoryArchive(payload);
        await loadHistory();
        alert(`Imported ${imported.length} shot${imported.length === 1 ? '' : 's'} into Shot History.`);
      } catch (error) {
        console.error('Failed to import shot history archive:', error);
        alert(`Import failed: ${error.message}`);
      } finally {
        event.target.value = '';
        setArchiveBusy(false);
      }
    },
    [loadHistory],
  );

  const { paginatedHistory, totalPages, totalFilteredItems } = useMemo(() => {
    let filtered = applyShotFilters(history, filters);

    if (searchTerm.trim()) {
      const search = searchTerm.toLowerCase().trim();
      filtered = filtered.filter(
        shot =>
          shot.profile?.toLowerCase().includes(search) ||
          shot.beanName?.toLowerCase().includes(search) ||
          String(shot.id).includes(search) ||
          String(shot.source || '').toLowerCase().includes(search),
      );
    }

    switch (filterBy) {
      case 'rated':
        filtered = filtered.filter(shot => shot.rating && shot.rating > 0);
        break;
      case 'unrated':
        filtered = filtered.filter(shot => !shot.rating || shot.rating === 0);
        break;
      case 'device':
        filtered = filtered.filter(shot => shot.source === 'gaggimate');
        break;
      case 'imported':
        filtered = filtered.filter(shot => shot.source === 'browser');
        break;
      default:
        break;
    }

    filtered.sort((a, b) => {
      let comparison = 0;

      switch (sortBy) {
        case 'date':
          comparison = a.timestamp - b.timestamp;
          break;
        case 'rating':
          comparison = (a.rating || 0) - (b.rating || 0);
          break;
        case 'profile':
          comparison = (a.profile || '').localeCompare(b.profile || '');
          break;
        case 'duration':
          comparison = a.duration - b.duration;
          break;
        case 'bean':
          comparison = (a.beanName || '').localeCompare(b.beanName || '');
          break;
        case 'volume':
          comparison = (a.volume || 0) - (b.volume || 0);
          break;
        default:
          comparison = a.timestamp - b.timestamp;
      }

      return sortOrder === 'desc' ? -comparison : comparison;
    });

    const totalFiltered = filtered.length;
    const pages = Math.max(1, Math.ceil(totalFiltered / itemsPerPage));
    const safeCurrentPage = Math.min(currentPage, pages);
    const startIndex = (safeCurrentPage - 1) * itemsPerPage;
    const endIndex = startIndex + itemsPerPage;

    const paginated = filtered.slice(startIndex, endIndex).map(shot => {
      if (!shot.beanName || !allBeans.length) return shot;
      const bean = shot.beanId
        ? allBeans.find(b => b.id === shot.beanId)
        : allBeans.find(b => b.name.trim().toLowerCase() === shot.beanName.trim().toLowerCase());
      // PRO-422: only flag "(archived)" when the bean was recorded by the device
      // (authoritative). An inferred/localStorage-guessed bean must not surface
      // the archived suffix, since the guess may bind to an unrelated bean.
      return bean && shot.beanRecorded ? { ...shot, beanArchived: !!bean.archived } : shot;
    });

    return {
      paginatedHistory: paginated,
      totalPages: pages,
      totalFilteredItems: totalFiltered,
    };
  }, [history, filters, searchTerm, filterBy, sortBy, sortOrder, currentPage, allBeans]);

  useEffect(() => {
    if (currentPage > totalPages) {
      setCurrentPage(totalPages);
    }
  }, [currentPage, totalPages]);

  // Dropdown options derived from the loaded history + bean library (PRO-31).
  const profileOptions = useMemo(() => availableProfiles(history), [history]);
  const beanOptions = useMemo(() => availableBeans(history, allBeans), [history, allBeans]);
  const filtersActive = hasActiveFilters(filters);

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
        <div>
          <h1 className='font-nd-mono text-[20px] uppercase tracking-[0.08em] text-[var(--text-secondary,#999)]'>
            Shot History
          </h1>
          <p className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)] mt-2'>
            {totalFilteredItems} / {history.length} shots
            {totalPages > 1 && ` • Page ${currentPage} of ${totalPages}`}
          </p>
        </div>
        <div className='flex items-center gap-2'>
          <button
            onClick={() => setFiltersOpen(open => !open)}
            className={`nd-action-btn${filtersActive ? ' nd-action-btn--primary' : ''}`}
            title='Filters'
            aria-label='Toggle filters'
            aria-expanded={filtersOpen}
          >
            <FontAwesomeIcon icon={faFilter} />
          </button>
          <button
            onClick={handleExportAll}
            className='nd-action-btn'
            disabled={archiveBusy || history.length === 0}
            title='Export History'
            aria-label='Export History'
          >
            <FontAwesomeIcon icon={faFileExport} />
          </button>
          <button
            onClick={() => importInputRef.current?.click()}
            className='nd-action-btn'
            disabled={archiveBusy}
            title='Import History'
          >
            <FontAwesomeIcon icon={faFileImport} />
          </button>
          <input
            ref={importInputRef}
            type='file'
            accept='.json,application/json'
            className='hidden'
            onChange={handleImportFile}
          />
        </div>
      </div>

      {/* Filters */}
      <Card sm={12} title='Shots'>
        <div className='flex flex-col gap-4'>
          {/* Search */}
          <div className='flex'>
            <input
              type='text'
              placeholder='Search shots...'
              value={searchTerm}
              onChange={e => {
                setSearchTerm(e.target.value);
                setCurrentPage(1);
              }}
              className='nd-input flex-1 rounded-r-none border-r-0'
            />
            <span className='nd-input-unit'>
              <FontAwesomeIcon icon={faSearch} className='text-[var(--text-disabled,#666)]' />
            </span>
          </div>

          {/* Sort + Filter row */}
          <div className='flex flex-wrap items-center gap-4'>
            <div className='flex items-center gap-2'>
              <FontAwesomeIcon icon={faSort} className='text-[var(--text-disabled,#666)]' />
              <select
                value={`${sortBy}-${sortOrder}`}
                onChange={e => {
                  const [newSortBy, newSortOrder] = e.target.value.split('-');
                  setSortBy(newSortBy);
                  setSortOrder(newSortOrder);
                  setCurrentPage(1);
                }}
                className='nd-input py-1 pr-8'
              >
                <option value='date-desc'>Newest First</option>
                <option value='date-asc'>Oldest First</option>
                <option value='rating-desc'>Highest Rated</option>
                <option value='rating-asc'>Lowest Rated</option>
                <option value='profile-asc'>Profile A-Z</option>
                <option value='profile-desc'>Profile Z-A</option>
                <option value='bean-asc'>Bean A-Z</option>
                <option value='bean-desc'>Bean Z-A</option>
                <option value='duration-desc'>Longest Duration</option>
                <option value='duration-asc'>Shortest Duration</option>
                <option value='volume-desc'>Highest Volume</option>
                <option value='volume-asc'>Lowest Volume</option>
              </select>
            </div>
            <div className='flex items-center gap-2'>
              <FontAwesomeIcon icon={faFilter} className='text-[var(--text-disabled,#666)]' />
              <select
                value={filterBy}
                onChange={e => {
                  setFilterBy(e.target.value);
                  setCurrentPage(1);
                }}
                className='nd-input py-1 pr-8'
              >
                <option value='all'>All Shots</option>
                <option value='device'>Device Only</option>
                <option value='imported'>Imported Only</option>
                <option value='rated'>Rated Only</option>
                <option value='unrated'>Unrated Only</option>
              </select>
            </div>
          </div>

          {/* Advanced filter panel (PRO-31) — collapsible via header toggle */}
          {filtersOpen && (
            <div className='flex flex-col gap-4 rounded-md border border-[var(--home-border,rgba(255,255,255,0.08))] p-4'>
              <div className='flex items-center justify-between gap-3'>
                <span className='font-nd-mono text-[12px] uppercase tracking-[0.08em] text-[var(--text-secondary,#999)]'>
                  Filters — {totalFilteredItems} / {history.length}
                </span>
                <button
                  type='button'
                  onClick={clearFilters}
                  disabled={!filtersActive}
                  className='nd-action-btn nd-action-btn--text flex items-center gap-2 px-3'
                  title='Clear filters'
                >
                  <FontAwesomeIcon icon={faXmark} />
                  <span className='font-nd-mono text-[12px]'>Clear filters</span>
                </button>
              </div>

              <div className='grid grid-cols-1 gap-4 sm:grid-cols-2'>
                {/* Date range */}
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    From date
                  </span>
                  <input
                    type='date'
                    value={filters.dateFrom}
                    max={filters.dateTo || undefined}
                    onChange={e => updateFilter('dateFrom', e.target.value)}
                    className='nd-input'
                  />
                </label>
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    To date
                  </span>
                  <input
                    type='date'
                    value={filters.dateTo}
                    min={filters.dateFrom || undefined}
                    onChange={e => updateFilter('dateTo', e.target.value)}
                    className='nd-input'
                  />
                </label>

                {/* Profile */}
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    Profile
                  </span>
                  <select
                    value={filters.profile}
                    onChange={e => updateFilter('profile', e.target.value)}
                    className='nd-input pr-8'
                  >
                    <option value=''>All profiles</option>
                    {profileOptions.map(name => (
                      <option key={name} value={name}>
                        {name}
                      </option>
                    ))}
                  </select>
                </label>

                {/* Bean */}
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    Bean
                  </span>
                  <select
                    value={filters.beanId}
                    onChange={e => updateFilter('beanId', e.target.value)}
                    className='nd-input pr-8'
                  >
                    <option value=''>All beans</option>
                    {beanOptions.map(bean => (
                      <option key={bean.id} value={bean.id}>
                        {bean.name}
                      </option>
                    ))}
                  </select>
                </label>

                {/* Duration range (seconds) */}
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    Min duration (s)
                  </span>
                  <input
                    type='number'
                    min='0'
                    inputMode='numeric'
                    placeholder='0'
                    value={filters.durationMin}
                    onChange={e => updateFilter('durationMin', e.target.value)}
                    className='nd-input'
                  />
                </label>
                <label className='flex flex-col gap-1'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    Max duration (s)
                  </span>
                  <input
                    type='number'
                    min='0'
                    inputMode='numeric'
                    placeholder='∞'
                    value={filters.durationMax}
                    onChange={e => updateFilter('durationMax', e.target.value)}
                    className='nd-input'
                  />
                </label>

                {/* Free text (notes) */}
                <label className='flex flex-col gap-1 sm:col-span-2'>
                  <span className='font-nd-mono text-[11px] uppercase tracking-[0.08em] text-[var(--text-disabled,#666)]'>
                    Notes contain
                  </span>
                  <input
                    type='text'
                    placeholder='Search shot notes...'
                    value={filters.text}
                    onChange={e => updateFilter('text', e.target.value)}
                    className='nd-input'
                  />
                </label>
              </div>
            </div>
          )}
        </div>

        {/* Shot list */}
        <div className='mt-5 flex flex-col gap-3'>
          {paginatedHistory.map(item => (
            <HistoryCard
              key={getHistoryKey(item)}
              shot={item}
              onDelete={() => onDelete(item)}
              onNotesChanged={onNotesChanged}
              onLoad={() => loadShotDetails(item)}
            />
          ))}
          {totalFilteredItems === 0 && !loading && (
            <div className='py-20 text-center'>
              <span className='font-nd-mono text-[14px] text-[var(--text-disabled,#666)]'>
                {history.length === 0 ? 'No shots available' : 'No shots match your search and filter criteria'}
              </span>
            </div>
          )}
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className='mt-6 flex items-center justify-center gap-2'>
            <button
              className='nd-action-btn'
              style={{ width: '36px', height: '36px' }}
              disabled={currentPage === 1}
              onClick={() => setCurrentPage(currentPage - 1)}
            >
              <FontAwesomeIcon icon={faChevronLeft} />
            </button>
            <div className='flex items-center gap-1'>
              {Array.from({ length: Math.min(5, totalPages) }, (_, i) => {
                let pageNum;
                if (totalPages <= 5) {
                  pageNum = i + 1;
                } else if (currentPage <= 3) {
                  pageNum = i + 1;
                } else if (currentPage >= totalPages - 2) {
                  pageNum = totalPages - 4 + i;
                } else {
                  pageNum = currentPage - 2 + i;
                }

                return (
                  <button
                    key={pageNum}
                    className={`nd-action-btn nd-action-btn--text ${currentPage === pageNum ? '' : ''}`}
                    style={{ width: '36px', height: '36px', minWidth: 'unset' }}
                    onClick={() => setCurrentPage(pageNum)}
                  >
                    {pageNum}
                  </button>
                );
              })}
            </div>
            <button
              className='nd-action-btn'
              style={{ width: '36px', height: '36px' }}
              disabled={currentPage === totalPages}
              onClick={() => setCurrentPage(currentPage + 1)}
            >
              <FontAwesomeIcon icon={faChevronRight} />
            </button>
          </div>
        )}
      </Card>
    </div>
  );
}
