import { useState, useEffect, useContext, useCallback, useRef } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { ApiServiceContext } from '../../services/ApiService.js';
import { Spinner } from '../../components/Spinner.jsx';
import { faEdit } from '@fortawesome/free-solid-svg-icons/faEdit';
import { faSave } from '@fortawesome/free-solid-svg-icons/faSave';
import { faClose } from '@fortawesome/free-solid-svg-icons/faClose';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { listBeans, syncBeanUsageFromNotes } from '../../utils/beanManager.js';
import {
  formatTenPointRating,
  getRatingFillPercent,
  normalizeTenPointRating,
} from '../../utils/ratings.js';

export default function ShotNotesCard({ shot, onNotesUpdate, onNotesLoaded }) {
  const apiService = useContext(ApiServiceContext);
  const notesKey = shot.source === 'browser' ? String(shot.storageKey || shot.name || shot.id || '') : shot.id;
  const [notes, setNotes] = useState({
    id: shot.id, rating: 0, beanId: '', beanType: '',
    doseIn: '', doseOut: '', ratio: '',
    grinder: '', grindSetting: '', balanceTaste: 'balanced', notes: '',
  });
  const [loading, setLoading] = useState(false);
  const [isEditing, setIsEditing] = useState(false);
  const [initialLoaded, setInitialLoaded] = useState(false);
  const [availableBeans, setAvailableBeans] = useState([]);
  const savedNotesRef = useRef(null);
  const beanFieldListId = `bean-options-${notesKey}`;

  const calculateRatio = useCallback((doseIn, doseOut) => {
    if (doseIn && doseOut && parseFloat(doseIn) > 0 && parseFloat(doseOut) > 0) {
      return (parseFloat(doseOut) / parseFloat(doseIn)).toFixed(2);
    }
    return '';
  }, []);

  useEffect(() => {
    if (initialLoaded) return;
    notesService.setApiService(apiService);
    const loadNotes = async () => {
      try {
        let loadedNotes = {
          id: notesKey, rating: 0, beanId: '', beanType: shot.beanName || '',
          doseIn: '', doseOut: '', ratio: '', grinder: '', grindSetting: '',
          balanceTaste: 'balanced', notes: '',
        };
        const savedNotes = await notesService.loadNotes(notesKey, shot.source || 'gaggimate');
        loadedNotes = { ...loadedNotes, ...savedNotes, id: notesKey };
        if (!loadedNotes.doseOut && shot.volume) {
          loadedNotes.doseOut = shot.volume.toFixed(1);
        }
        if (loadedNotes.doseIn && loadedNotes.doseOut) {
          loadedNotes.ratio = calculateRatio(loadedNotes.doseIn, loadedNotes.doseOut);
        }
        setNotes(loadedNotes);
        savedNotesRef.current = loadedNotes;
        setInitialLoaded(true);
        if (onNotesLoaded) onNotesLoaded(loadedNotes);
      } catch (error) {
        console.error('Failed to load notes:', error);
        const defaultNotes = {
          id: notesKey, rating: 0, beanId: '', beanType: shot.beanName || '',
          doseIn: shot.volume ? shot.volume.toFixed(1) : '', ratio: '',
          grinder: '', grindSetting: '', balanceTaste: 'balanced', notes: '',
        };
        setNotes(defaultNotes);
        savedNotesRef.current = defaultNotes;
        setInitialLoaded(true);
        if (onNotesLoaded) onNotesLoaded(defaultNotes);
      }
    };
    loadNotes();
  }, []);

  useEffect(() => {
    if (notes.id !== notesKey) {
      setInitialLoaded(false);
      setIsEditing(false);
      savedNotesRef.current = null;
    }
  }, [notes.id, notesKey]);

  useEffect(() => {
    let cancelled = false;
    const loadAvailableBeans = async () => {
      try {
        const beans = await listBeans(apiService);
        if (!cancelled) setAvailableBeans(beans.filter(bean => !bean.archived));
      } catch (error) {
        console.error('Failed to load beans for shot notes:', error);
        if (!cancelled) setAvailableBeans([]);
      }
    };
    loadAvailableBeans();
    const handleBeansChanged = () => loadAvailableBeans();
    window.addEventListener('beans-library-changed', handleBeansChanged);
    return () => { cancelled = true; window.removeEventListener('beans-library-changed', handleBeansChanged); };
  }, [apiService]);

  useEffect(() => {
    if (!availableBeans.length || notes.beanId || !notes.beanType) return;
    const matchedBean = availableBeans.find(
      bean => String(bean.name || '').trim().toLowerCase() === String(notes.beanType || '').trim().toLowerCase(),
    );
    if (matchedBean) setNotes(prev => ({ ...prev, beanId: matchedBean.id }));
  }, [availableBeans, notes.beanId, notes.beanType]);

  const saveNotes = async () => {
    setLoading(true);
    try {
      await notesService.saveNotes(notesKey, shot.source || 'gaggimate', notes);
      await syncBeanUsageFromNotes(apiService, savedNotesRef.current, notes);
      savedNotesRef.current = notes;
      setIsEditing(false);
      if (onNotesUpdate) onNotesUpdate(notes);
    } catch (error) {
      console.error('Failed to save notes:', error);
    } finally {
      setLoading(false);
    }
  };

  const handleInputChange = (field, value) => {
    setNotes(prev => {
      const matchedBean = field === 'beanType'
        ? availableBeans.find(bean => normalizeBeanName(bean.name) === normalizeBeanName(value))
        : availableBeans.find(bean => bean.id === prev.beanId) || null;
      const newNotes = {
        ...prev,
        [field]: field === 'rating' ? normalizeTenPointRating(value) : value,
      };
      if (field === 'beanType') newNotes.beanId = matchedBean?.id || '';
      if ((field === 'doseIn' || field === 'doseOut') && initialLoaded) {
        const doseIn = field === 'doseIn' ? value : prev.doseIn;
        const doseOut = field === 'doseOut' ? value : prev.doseOut;
        newNotes.ratio = calculateRatio(doseIn, doseOut);
      }
      return newNotes;
    });
  };

  const normalizeBeanName = value => String(value || '').trim().toLowerCase();

  const renderStars = rating => (
    <div class="relative inline-flex text-lg leading-none">
      <div class="text-[--text-muted]">★★★★★</div>
      <div class="absolute inset-y-0 left-0 overflow-hidden whitespace-nowrap text-yellow-400" style={{ width: getRatingFillPercent(rating) }}>
        ★★★★★
      </div>
    </div>
  );

  const getTasteColor = taste => {
    switch (taste) {
      case 'bitter': return 'text-orange-400';
      case 'sour': return 'text-amber-400';
      case 'balanced': return 'text-emerald-400';
      default: return 'text-[--text-secondary]';
    }
  };

  const FieldLabel = ({ children }) => (
    <label class="block text-xs font-medium text-[--text-secondary] mb-1.5">{children}</label>
  );

  const FieldValue = ({ children }) => (
    <div class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary]" style="background: var(--bg-elevated); border: 1px solid var(--border);">
      {children || '\u2014'}
    </div>
  );

  const StyledInput = ({ type = 'text', value, placeholder, onChange, step, min, max, list }) => (
    <input
      type={type}
      value={value}
      placeholder={placeholder}
      step={step}
      min={min}
      max={max}
      list={list}
      onChange={e => onChange(e.target.value)}
      class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted]"
      style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
      onFocus={e => e.target.style.borderColor = 'var(--accent)'}
      onBlur={e => e.target.style.borderColor = 'var(--border)'}
    />
  );

  if (!initialLoaded) {
    return (
      <div class="flex items-center justify-center py-8 mt-3">
        <div class="size-5 rounded-full border-2 border-[--accent] border-t-transparent animate-spin" />
      </div>
    );
  }

  return (
    <div class="mt-4 pt-4" style="border-top: 1px solid var(--border);">
      {/* Header */}
      <div class="flex items-center justify-between mb-5">
        <h3 class="text-sm font-semibold text-[--text-primary]">Shot Notes</h3>
        {!isEditing ? (
          <button
            onClick={() => setIsEditing(true)}
            class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium text-[--text-secondary] border border-[--border] hover:border-[--accent] hover:text-[--accent] transition-all"
          >
            <FontAwesomeIcon icon={faEdit} />
            Edit
          </button>
        ) : (
          <div class="flex gap-2">
            <button
              onClick={() => { setIsEditing(false); setNotes(savedNotesRef.current || notes); }}
              disabled={loading}
              class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] transition-all"
            >
              <FontAwesomeIcon icon={faClose} />
              Cancel
            </button>
            <button
              onClick={saveNotes}
              disabled={loading}
              class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all"
              style="background: var(--accent); color: var(--bg-base);"
            >
              {loading ? <div class="size-3.5 rounded-full border-2 border-t-transparent animate-spin" style="border-color: var(--bg-base);" /> : <FontAwesomeIcon icon={faSave} />}
              Save
            </button>
          </div>
        )}
      </div>

      {/* Fields grid */}
      <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
        {/* Rating */}
        <div>
          <FieldLabel>Rating</FieldLabel>
          <div class="flex items-center gap-2">
            {renderStars(notes.rating)}
            {isEditing ? (
              <input
                type="number"
                min="0"
                max="10"
                step="0.25"
                value={notes.rating || ''}
                onChange={e => handleInputChange('rating', e.target.value)}
                placeholder="0-10"
                class="px-2 py-1 rounded-lg text-sm w-16 text-center"
                style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none; color: var(--text-primary);"
                onFocus={e => e.target.style.borderColor = 'var(--accent)'}
                onBlur={e => e.target.style.borderColor = 'var(--border)'}
              />
            ) : (
              <span class="text-sm font-medium text-[--text-primary]">{formatTenPointRating(notes.rating)}</span>
            )}
          </div>
        </div>

        {/* Bean Type */}
        <div>
          <FieldLabel>Bean Type</FieldLabel>
          {isEditing ? (
            <>
              <input
                type="text"
                list={beanFieldListId}
                value={notes.beanType}
                onChange={e => handleInputChange('beanType', e.target.value)}
                placeholder="Choose or type..."
                class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted]"
                style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
                onFocus={e => e.target.style.borderColor = 'var(--accent)'}
                onBlur={e => e.target.style.borderColor = 'var(--border)'}
              />
              <datalist id={beanFieldListId}>
                {availableBeans.map(bean => <option key={bean.id} value={bean.name} />)}
              </datalist>
            </>
          ) : (
            <FieldValue>{notes.beanType}</FieldValue>
          )}
        </div>

        {/* Dose In */}
        <div>
          <FieldLabel>Dose In (g)</FieldLabel>
          {isEditing ? (
            <StyledInput type="number" step="0.1" value={notes.doseIn} placeholder="18.0" onChange={v => handleInputChange('doseIn', v)} />
          ) : (
            <FieldValue>{notes.doseIn}g</FieldValue>
          )}
        </div>

        {/* Dose Out */}
        <div>
          <FieldLabel>Dose Out (g)</FieldLabel>
          {isEditing ? (
            <StyledInput type="number" step="0.1" value={notes.doseOut} placeholder="36.0" onChange={v => handleInputChange('doseOut', v)} />
          ) : (
            <FieldValue>{notes.doseOut}g</FieldValue>
          )}
        </div>

        {/* Ratio */}
        <div>
          <FieldLabel>Ratio</FieldLabel>
          <FieldValue>{notes.ratio ? `1:${notes.ratio}` : '\u2014'}</FieldValue>
        </div>

        {/* Grinder */}
        <div>
          <FieldLabel>Grinder</FieldLabel>
          {isEditing ? (
            <StyledInput type="text" value={notes.grinder} placeholder="e.g., Niche Zero" onChange={v => handleInputChange('grinder', v)} />
          ) : (
            <FieldValue>{notes.grinder}</FieldValue>
          )}
        </div>

        {/* Grind Setting */}
        <div>
          <FieldLabel>Grind Setting</FieldLabel>
          {isEditing ? (
            <StyledInput type="text" value={notes.grindSetting} placeholder="e.g., 2.5" onChange={v => handleInputChange('grindSetting', v)} />
          ) : (
            <FieldValue>{notes.grindSetting}</FieldValue>
          )}
        </div>

        {/* Balance/Taste */}
        <div>
          <FieldLabel>Balance / Taste</FieldLabel>
          {isEditing ? (
            <select
              value={notes.balanceTaste}
              onChange={e => handleInputChange('balanceTaste', e.target.value)}
              class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary] cursor-pointer"
              style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
            >
              <option value="bitter">Bitter</option>
              <option value="balanced">Balanced</option>
              <option value="sour">Sour</option>
            </select>
          ) : (
            <div class={`w-full px-3 py-2 rounded-lg text-sm font-medium ${getTasteColor(notes.balanceTaste)}`} style="background: var(--bg-elevated); border: 1px solid var(--border);">
              {notes.balanceTaste}
            </div>
          )}
        </div>
      </div>

      {/* Notes - full width */}
      <div class="mt-4">
        <FieldLabel>Notes {isEditing && <span class="text-[--text-muted]">({notes.notes.length}/200)</span>}</FieldLabel>
        {isEditing ? (
          <textarea
            value={notes.notes}
            maxLength={200}
            onChange={e => handleInputChange('notes', e.target.value)}
            placeholder="Tasting notes, observations..."
            rows={3}
            class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] resize-none"
            style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
            onFocus={e => e.target.style.borderColor = 'var(--accent)'}
            onBlur={e => e.target.style.borderColor = 'var(--border)'}
          />
        ) : (
          <div class="w-full px-3 py-2 min-h-[4rem] rounded-lg text-sm text-[--text-secondary]" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            {notes.notes || 'No notes added'}
          </div>
        )}
      </div>
    </div>
  );
}