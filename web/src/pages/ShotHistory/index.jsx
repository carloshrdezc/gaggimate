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
Chart.register(LineController, TimeScale, LinearScale, CategoryScale, PointElement, LineElement, Filler, Legend);

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { useCallback, useEffect, useRef, useState, useContext, useMemo } from 'preact/hooks';
import { computed } from '@preact/signals';
import HistoryCard from './HistoryCard.jsx';
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
import { faTimeline } from '@fortawesome/free-solid-svg-icons/faTimeline';
import { inferBeanForShot } from '../../utils/beanManager.js';

const connected = computed(() => machine.value.connected);

function getHistoryKey(shot) {
  return `${shot.source || 'gaggimate'}:${shot.id}`;
}

function normalizeBrowserShot(shot) {
  return { ...shot, source: 'browser', loaded: Array.isArray(shot.samples) && shot.samples.length > 0 };
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

  const enrichShotWithBean = useCallback(shot => ({ ...shot, beanName: inferBeanForShot(shot) }), []);

  useEffect(() => { notesService.setApiService(apiService); }, [apiService]);

  const mergeShots = useCallback((deviceShots, browserShots) => {
    setHistory(prev => {
      const existingMap = new Map(prev.map(shot => [getHistoryKey(shot), shot]));
      const nextShots = [...deviceShots, ...browserShots].map(shot => {
        const key = getHistoryKey(shot);
        const existing = existingMap.get(key);
        if (shot.source === 'gaggimate' && existing?.loaded) {
          return enrichShotWithBean({ ...existing, ...shot, rating: existing.rating ?? shot.rating, volume: shot.volume ?? existing.volume, incomplete: shot.incomplete ?? existing.incomplete, loaded: true });
        }
        if (shot.source === 'browser' && existing) {
          return enrichShotWithBean({ ...existing, ...shot, loaded: shot.loaded || existing.loaded });
        }
        return enrichShotWithBean(shot);
      });
      return nextShots.sort((a, b) => b.timestamp - a.timestamp);
    });
  }, [enrichShotWithBean]);

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
        const loadedShot = enrichShotWithBean({ ...shot, ...storedShot, source: 'browser', loaded: true });
        setHistory(prev => prev.map(item => getHistoryKey(item) === getHistoryKey(shot) ? loadedShot : item));
        return loadedShot;
      }
      const paddedId = String(shot.id).padStart(6, '0');
      const resp = await fetch(`/api/history/${paddedId}.slog`);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const buf = await resp.arrayBuffer();
      const parsed = parseBinaryShot(buf, shot.id);
      parsed.incomplete = (shot?.incomplete ?? false) || parsed.incomplete;
      if (shot?.notes) parsed.notes = shot.notes;
      const loadedShot = enrichShotWithBean({ ...shot, ...parsed, volume: shot.volume ?? parsed.volume, rating: shot.rating ?? parsed.rating, incomplete: shot.incomplete ?? parsed.incomplete, source: 'gaggimate', loaded: true });
      setHistory(prev => prev.map(item => getHistoryKey(item) === getHistoryKey(shot) ? loadedShot : item));
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
      return { ...(storedShot || shot), id: String(shot.id), source: 'browser', loaded: Array.isArray((storedShot || shot)?.samples) && (storedShot || shot).samples.length > 0, notes };
    }
    const notes = await notesService.loadNotes(String(shot.id), 'gaggimate');
    return { ...shot, id: String(shot.id), source: 'gaggimate', loaded: Array.isArray(shot.samples) && shot.samples.length > 0, volume: shot.volume ?? null, rating: notes.rating || shot.rating || 0, incomplete: shot.incomplete ?? false, notes };
  }, []);

  const onDelete = useCallback(async shot => {
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
  }, [apiService, loadHistory]);

  const onNotesChanged = useCallback((id, notes, source) => {
    setHistory(prev => prev.map(shot =>
      shot.id === id && shot.source === source ? { ...shot, notes, rating: notes.rating || 0 } : shot
    ));
  }, []);

  const handleExportAll = useCallback(async () => {
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    const filename = `shot-history-${stamp}.json`;
    const download = prepareDownload(filename);
    setArchiveBusy(true);
    try {
      const exportShots = [];
      for (const shot of history) exportShots.push(await buildExportShot(shot));
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

  const handleImportFile = useCallback(async event => {
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
  }, [loadHistory]);

  const { paginatedHistory, totalPages, totalFilteredItems } = useMemo(() => {
    let filtered = [...history];
    if (searchTerm.trim()) {
      const search = searchTerm.toLowerCase().trim();
      filtered = filtered.filter(shot =>
        shot.profile?.toLowerCase().includes(search) ||
        shot.beanName?.toLowerCase().includes(search) ||
        String(shot.id).includes(search) ||
        String(shot.source || '').toLowerCase().includes(search)
      );
    }
    switch (filterBy) {
      case 'rated': filtered = filtered.filter(shot => shot.rating && shot.rating > 0); break;
      case 'unrated': filtered = filtered.filter(shot => !shot.rating || shot.rating === 0); break;
      case 'device': filtered = filtered.filter(shot => shot.source === 'gaggimate'); break;
      case 'imported': filtered = filtered.filter(shot => shot.source === 'browser'); break;
    }
    filtered.sort((a, b) => {
      let comparison = 0;
      switch (sortBy) {
        case 'date': comparison = a.timestamp - b.timestamp; break;
        case 'rating': comparison = (a.rating || 0) - (b.rating || 0); break;
        case 'profile': comparison = (a.profile || '').localeCompare(b.profile || ''); break;
        case 'duration': comparison = a.duration - b.duration; break;
        case 'bean': comparison = (a.beanName || '').localeCompare(b.beanName || ''); break;
        case 'volume': comparison = (a.volume || 0) - (b.volume || 0); break;
        default: comparison = a.timestamp - b.timestamp;
      }
      return sortOrder === 'desc' ? -comparison : comparison;
    });
    const totalFiltered = filtered.length;
    const pages = Math.max(1, Math.ceil(totalFiltered / itemsPerPage));
    const safeCurrentPage = Math.min(currentPage, pages);
    const startIndex = (safeCurrentPage - 1) * itemsPerPage;
    return { paginatedHistory: filtered.slice(startIndex, startIndex + itemsPerPage), totalPages: pages, totalFilteredItems: totalFiltered };
  }, [history, searchTerm, filterBy, sortBy, sortOrder, currentPage]);

  useEffect(() => { if (currentPage > totalPages) setCurrentPage(totalPages); }, [currentPage, totalPages]);

  if (loading) {
    return (
      <div class="space-y-3">
        <div class="h-8 w-32 rounded-lg skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        {[1, 2, 3].map(i => (
          <div key={i} class="h-24 rounded-xl skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        ))}
      </div>
    );
  }

  return (
    <div class="space-y-6">
      {/* Page Header */}
      <div class="flex flex-col gap-4 md:flex-row md:items-start md:justify-between">
        <div>
          <h1 class="text-2xl font-semibold text-[--text-primary]">Shot History</h1>
          <p class="text-sm text-[--text-secondary] mt-1">
            {totalFilteredItems} of {history.length} shots{totalPages > 1 ? ` · Page ${currentPage} of ${totalPages}` : ''}
          </p>
        </div>
        <div class="flex gap-2">
          <button
            onClick={handleExportAll}
            disabled={archiveBusy || history.length === 0}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all disabled:opacity-50"
          >
            <FontAwesomeIcon icon={faFileExport} />
            <span>Export</span>
          </button>
          <button
            onClick={() => importInputRef.current?.click()}
            disabled={archiveBusy}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
          >
            <FontAwesomeIcon icon={faFileImport} />
            <span>Import</span>
          </button>
          <input ref={importInputRef} type="file" accept=".json,application/json" class="hidden" onChange={handleImportFile} />
        </div>
      </div>

      {/* Search + Filter */}
      <div class="flex flex-col gap-3 xl:flex-row xl:items-center">
        <div class="relative flex-grow max-w-md">
          <FontAwesomeIcon icon={faSearch} class="absolute left-3 top-1/2 -translate-y-1/2 text-[--text-muted]" />
          <input
            type="text"
            placeholder="Search shots..."
            value={searchTerm}
            onChange={e => { setSearchTerm(e.target.value); setCurrentPage(1); }}
            class="w-full pl-10 pr-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted]"
            style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
            onFocus={e => e.target.style.borderColor = 'var(--accent)'}
            onBlur={e => e.target.style.borderColor = 'var(--border)'}
          />
        </div>

        <div class="flex items-center gap-2 flex-wrap">
          <FontAwesomeIcon icon={faSort} class="text-[--text-muted]" />
          <select
            value={`${sortBy}-${sortOrder}`}
            onChange={e => {
              const [newSortBy, newSortOrder] = e.target.value.split('-');
              setSortBy(newSortBy);
              setSortOrder(newSortOrder);
              setCurrentPage(1);
            }}
            class="px-3 py-2 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
            style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
            onFocus={e => e.target.style.borderColor = 'var(--accent)'}
            onBlur={e => e.target.style.borderColor = 'var(--border)'}
          >
            <option value="date-desc">Newest First</option>
            <option value="date-asc">Oldest First</option>
            <option value="rating-desc">Highest Rated</option>
            <option value="rating-asc">Lowest Rated</option>
            <option value="profile-asc">Profile A-Z</option>
            <option value="profile-desc">Profile Z-A</option>
            <option value="bean-asc">Bean A-Z</option>
            <option value="bean-desc">Bean Z-A</option>
            <option value="duration-desc">Longest</option>
            <option value="duration-asc">Shortest</option>
            <option value="volume-desc">Highest Vol</option>
            <option value="volume-asc">Lowest Vol</option>
          </select>

          <FontAwesomeIcon icon={faFilter} class="text-[--text-muted]" />
          <select
            value={filterBy}
            onChange={e => { setFilterBy(e.target.value); setCurrentPage(1); }}
            class="px-3 py-2 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
            style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
            onFocus={e => e.target.style.borderColor = 'var(--accent)'}
            onBlur={e => e.target.style.borderColor = 'var(--border)'}
          >
            <option value="all">All Shots</option>
            <option value="device">Device Only</option>
            <option value="imported">Imported Only</option>
            <option value="rated">Rated Only</option>
            <option value="unrated">Unrated Only</option>
          </select>
        </div>
      </div>

      {/* Shot list */}
      <div class="space-y-2">
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
          <div class="flex flex-col items-center justify-center py-16 text-center">
            <FontAwesomeIcon icon={faTimeline} class="text-4xl text-[--text-muted] mb-3" />
            <p class="text-[--text-muted]">
              {history.length === 0 ? 'No shots available.' : 'No shots match your search and filter.'}
            </p>
          </div>
        )}
      </div>

      {/* Pagination */}
      {totalPages > 1 && (
        <div class="flex items-center justify-center gap-2">
          <button
            onClick={() => setCurrentPage(currentPage - 1)}
            disabled={currentPage === 1}
            class="px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all disabled:opacity-50"
          >
            Previous
          </button>

          <div class="flex items-center gap-1">
            {Array.from({ length: Math.min(5, totalPages) }, (_, i) => {
              let pageNum;
              if (totalPages <= 5) pageNum = i + 1;
              else if (currentPage <= 3) pageNum = i + 1;
              else if (currentPage >= totalPages - 2) pageNum = totalPages - 4 + i;
              else pageNum = currentPage - 2 + i;
              return (
                <button
                  key={pageNum}
                  onClick={() => setCurrentPage(pageNum)}
                  class="size-9 rounded-lg text-sm font-medium transition-all"
                  style={currentPage === pageNum ? 'background: var(--accent); color: var(--bg-base);' : 'color: var(--text-secondary); border: 1px solid var(--border);'}
                >
                  {pageNum}
                </button>
              );
            })}
          </div>

          <button
            onClick={() => setCurrentPage(currentPage + 1)}
            disabled={currentPage === totalPages}
            class="px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all disabled:opacity-50"
          >
            Next
          </button>
        </div>
      )}
    </div>
  );
}