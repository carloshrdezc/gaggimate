/**
 * NotesBar.jsx
 * Compact horizontal metadata bar below the StatusBar.
 * Click anywhere (except nav arrows) to expand the notes panel.
 * Edit mode lives in the expanded panel with vertical layout.
 */

import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faChevronLeft } from '@fortawesome/free-solid-svg-icons/faChevronLeft';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faWeightScale } from '@fortawesome/free-solid-svg-icons/faWeightScale';
import { faStar } from '@fortawesome/free-solid-svg-icons/faStar';
import { faDivide } from '@fortawesome/free-solid-svg-icons/faDivide';
import { faTag } from '@fortawesome/free-solid-svg-icons/faTag';
import { faGears } from '@fortawesome/free-solid-svg-icons/faGears';
import { notesService } from '../services/NotesService';
import { cleanName, getNotesTasteStyle } from '../utils/analyzerUtils';
import { NotesBarExpanded } from './NotesBarExpanded';
import { formatTenPointRating, normalizeTenPointRating } from '../../../utils/ratings';

const getTasteTextStyle = taste => {
  const tasteStyle = getNotesTasteStyle(taste);
  return tasteStyle ? { color: tasteStyle.color } : undefined;
};

export function NotesBar({
  currentShot,
  currentShotName,
  shotList = [],
  onNavigate,
  notesExpanded = false,
  onToggleNotesExpanded,
  onEditingChange,
  onExpandedHeightChange,
}) {
  const chipGap = 'clamp(0.35rem, 0.9vw, 0.7rem)';

  const getShotNotesKey = useCallback(shot => {
    if (!shot) return '';
    if (shot.source === 'gaggimate') return String(shot.id || '');
    return String(shot.storageKey || shot.name || shot.id || '');
  }, []);

  const [notes, setNotes] = useState(notesService.getDefaults(null));
  const [isEditing, setIsEditing] = useState(false);
  const [saving, setSaving] = useState(false);
  const [loading, setLoading] = useState(false);
  const expandedPanelRef = useRef(null);

  const calculateRatio = useCallback((doseIn, doseOut) => {
    if (doseIn && doseOut && parseFloat(doseIn) > 0 && parseFloat(doseOut) > 0) {
      return (parseFloat(doseOut) / parseFloat(doseIn)).toFixed(2);
    }
    return '';
  }, []);

  const extractDoseFromProfile = useCallback(profileName => {
    if (!profileName) return '';
    const match = profileName.match(/\[?\b(\d+(?:\.\d+)?)\s*g\b\]?/i);
    return match ? match[1] : '';
  }, []);

  useEffect(() => {
    if (!currentShot) return;
    let cancelled = false;
    const notesKey = getShotNotesKey(currentShot);
    const inlineNotes =
      currentShot.notes && typeof currentShot.notes === 'object'
        ? { ...notesService.getDefaults(notesKey), ...currentShot.notes, id: notesKey }
        : null;
    setLoading(true);
    setIsEditing(false);

    if (inlineNotes) {
      setNotes(inlineNotes);
    }

    notesService
      .loadNotes(notesKey, currentShot.source)
      .then(loaded => {
        if (cancelled) return;
        loaded = inlineNotes ? { ...loaded, ...inlineNotes, id: notesKey } : loaded;
        let autoSave = false;

        if (!loaded.doseIn && currentShot.profile) {
          const extracted = extractDoseFromProfile(currentShot.profile);
          if (extracted) {
            loaded.doseIn = extracted;
            autoSave = true;
          }
        }

        if (!loaded.doseOut && currentShot.volume) {
          loaded.doseOut = currentShot.volume.toFixed(1);
          autoSave = true;
        }

        if (loaded.doseIn && loaded.doseOut) {
          loaded.ratio = calculateRatio(loaded.doseIn, loaded.doseOut);
        }

        setNotes(loaded);

        if (autoSave && currentShot.source !== 'temp') {
          notesService.saveNotes(notesKey, currentShot.source, loaded);
        }
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });

    return () => {
      cancelled = true;
    };
  }, [
    currentShot,
    currentShot?.id,
    currentShot?.name,
    currentShot?.storageKey,
    currentShot?.source,
    calculateRatio,
    extractDoseFromProfile,
    getShotNotesKey,
  ]);

  const handleInputChange = (field, value) => {
    setNotes(prev => {
      const updated = {
        ...prev,
        [field]: field === 'rating' ? normalizeTenPointRating(value) : value,
      };
      if (field === 'doseIn' || field === 'doseOut') {
        const dIn = field === 'doseIn' ? value : prev.doseIn;
        const dOut = field === 'doseOut' ? value : prev.doseOut;
        updated.ratio = calculateRatio(dIn, dOut);
      }
      return updated;
    });
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      await notesService.saveNotes(getShotNotesKey(currentShot), currentShot.source, notes);
      setIsEditing(false);
    } catch (e) {
      console.error('Failed to save notes:', e);
    } finally {
      setSaving(false);
    }
  };

  const handleCancel = () => {
    setIsEditing(false);
    notesService.loadNotes(getShotNotesKey(currentShot), currentShot.source).then(loaded => {
      if (loaded.doseIn && loaded.doseOut) {
        loaded.ratio = calculateRatio(loaded.doseIn, loaded.doseOut);
      }
      setNotes(loaded);
    });
  };

  const currentIndex = shotList.findIndex(
    s => getShotNotesKey(s) === getShotNotesKey(currentShot) && s.source === currentShot?.source,
  );
  const canGoPrev = currentIndex > 0;
  const canGoNext = currentIndex >= 0 && currentIndex < shotList.length - 1;

  useEffect(() => {
    if (!currentShot) return;

    const isTypingTarget = target => {
      const el = target instanceof Element ? target : document.activeElement;
      if (!el) return false;
      const tag = el.tagName?.toLowerCase();
      if (el.isContentEditable) return true;
      if (tag === 'input' || tag === 'textarea' || tag === 'select') return true;
      return !!el.closest(
        'input, textarea, select, [contenteditable="true"], [role="textbox"]',
      );
    };

    const handleKeyDown = e => {
      if (e.defaultPrevented || e.metaKey || e.ctrlKey || e.altKey || isTypingTarget(e.target)) {
        return;
      }

      if (e.key === 'ArrowLeft' && canGoPrev) {
        e.preventDefault();
        onNavigate(shotList[currentIndex - 1]);
      } else if (e.key === 'ArrowRight' && canGoNext) {
        e.preventDefault();
        onNavigate(shotList[currentIndex + 1]);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [currentShot, canGoPrev, canGoNext, currentIndex, shotList, onNavigate]);

  useEffect(() => {
    onEditingChange?.(isEditing);
  }, [isEditing, onEditingChange]);

  useEffect(() => {
    if (!notesExpanded) {
      onExpandedHeightChange?.(0);
      return;
    }

    const panelEl = expandedPanelRef.current;
    if (!panelEl) {
      onExpandedHeightChange?.(0);
      return;
    }

    const reportHeight = () => onExpandedHeightChange?.(panelEl.offsetHeight || 0);
    reportHeight();

    if (typeof ResizeObserver === 'undefined') return undefined;
    const resizeObserver = new ResizeObserver(reportHeight);
    resizeObserver.observe(panelEl);
    return () => resizeObserver.disconnect();
  }, [notesExpanded, onExpandedHeightChange]);

  const sourceLabel = currentShot?.source === 'gaggimate' ? 'GM' : 'WEB';
  const sourceBadgeClass =
    currentShot?.source === 'gaggimate'
      ? 'bg-blue-500/10 text-blue-500'
      : 'bg-purple-500/10 text-purple-500';

  const formatDateTime = ts => {
    if (!ts) return '\u2014';
    const d = new Date(ts * 1000);
    const day = String(d.getDate()).padStart(2, '0');
    const month = String(d.getMonth() + 1).padStart(2, '0');
    const year = d.getFullYear();
    const hours = String(d.getHours()).padStart(2, '0');
    const mins = String(d.getMinutes()).padStart(2, '0');
    return `${day}.${month}.${year} ${hours}:${mins}`;
  };

  const getDuration = () => {
    if (!currentShot?.samples?.length) return '\u2014';
    const first = currentShot.samples[0].t;
    const last = currentShot.samples[currentShot.samples.length - 1].t;
    return `${Math.round((last - first) / 1000)}s`;
  };

  const getShotDisplayName = () => {
    if (currentShot?.source === 'gaggimate') {
      return `#${currentShot.id}`;
    }
    return cleanName(currentShotName);
  };

  if (!currentShot) return null;

  const borderClasses = 'border-base-content/5 border-t';
  const fieldCls =
    'shrink-0 rounded-md bg-base-200/60 px-2 py-1 text-xs font-medium whitespace-nowrap';

  return (
    <div>
      <div className={`transition-all duration-200 ${borderClasses}`}>
        <div className='flex w-full items-center px-1 py-0.5' style={{ columnGap: chipGap }}>
          <button
            className='btn btn-xs btn-ghost flex-shrink-0 rounded-lg px-1.5 py-1.5 disabled:opacity-30'
            disabled={!canGoPrev}
            onClick={() => canGoPrev && onNavigate(shotList[currentIndex - 1])}
            title='Previous shot'
          >
            <FontAwesomeIcon icon={faChevronLeft} />
          </button>

          <button
            type='button'
            className='scrollbar-none block min-w-0 flex-1 cursor-pointer overflow-x-auto px-1 py-1.5'
            onClick={() => !isEditing && onToggleNotesExpanded && onToggleNotesExpanded()}
            title='Click to expand notes'
          >
            <div className='mx-auto flex w-max items-center' style={{ columnGap: chipGap }}>
              <span
                className={`shrink-0 rounded px-1.5 py-0.5 text-[9px] font-bold uppercase ${sourceBadgeClass}`}
              >
                {sourceLabel}
              </span>
              <span className={fieldCls}>{getShotDisplayName()}</span>
              <span className={fieldCls}>{cleanName(currentShot.profile || '\u2014')}</span>
              <span className={fieldCls}>{formatDateTime(currentShot.timestamp)}</span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faClock} className='text-[10px] opacity-50' />
                {getDuration()}
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faDivide} className='text-[10px] opacity-50' />
                {notes.ratio ? `1:${notes.ratio}` : '\u2014'}
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faWeightScale} className='text-[10px] opacity-50' />
                {notes.doseIn || '\u2014'}g to {notes.doseOut || '\u2014'}g
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon
                  icon={faStar}
                  className={`text-[10px] ${notes.rating > 0 ? 'text-yellow-400' : 'opacity-30'}`}
                />
                {formatTenPointRating(notes.rating)}
              </span>
              <span
                className={`${fieldCls} capitalize`}
                style={getTasteTextStyle(notes.balanceTaste)}
              >
                {notes.balanceTaste}
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faTag} className='text-[10px] opacity-50' />
                {notes.beanType || '\u2014'}
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faGears} className='text-[10px] opacity-50' />
                {notes.grinder || '\u2014'}
              </span>
              <span className={`${fieldCls} flex items-center gap-1`}>
                <FontAwesomeIcon icon={faGears} className='text-[10px] opacity-50' />
                {notes.grindSetting || '\u2014'}
              </span>
            </div>
          </button>

          <button
            className='btn btn-xs btn-ghost flex-shrink-0 rounded-lg px-1.5 py-1.5 disabled:opacity-30'
            disabled={!canGoNext}
            onClick={() => canGoNext && onNavigate(shotList[currentIndex + 1])}
            title='Next shot'
          >
            <FontAwesomeIcon icon={faChevronRight} />
          </button>
        </div>

        {loading && (
          <div className='bg-primary/20 h-0.5 w-full'>
            <div className='bg-primary h-full w-1/3 animate-pulse rounded-full' />
          </div>
        )}
      </div>

      {notesExpanded && (
        <div ref={expandedPanelRef}>
          <NotesBarExpanded
            currentShot={currentShot}
            notes={notes}
            isEditing={isEditing}
            saving={saving}
            onInputChange={handleInputChange}
            onEdit={() => setIsEditing(true)}
            onSave={handleSave}
            onCancel={handleCancel}
            onCollapse={onToggleNotesExpanded}
          />
        </div>
      )}
    </div>
  );
}
