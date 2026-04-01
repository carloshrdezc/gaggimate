import { useState, useEffect, useContext, useCallback, useRef } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { ApiServiceContext } from '../../services/ApiService.js';
import { Spinner } from '../../components/Spinner.jsx';
import { faEdit } from '@fortawesome/free-solid-svg-icons/faEdit';
import { faSave } from '@fortawesome/free-solid-svg-icons/faSave';
import { notesService } from '../ShotAnalyzer/services/NotesService.js';
import { listBeans, syncBeanUsageFromNotes } from '../../utils/beanManager.js';
import {
  formatTenPointRating,
  getRatingFillPercent,
  normalizeTenPointRating,
} from '../../utils/ratings.js';

export default function ShotNotesCard({ shot, onNotesUpdate, onNotesLoaded }) {
  const apiService = useContext(ApiServiceContext);
  const notesKey =
    shot.source === 'browser' ? String(shot.storageKey || shot.name || shot.id || '') : shot.id;

  const [notes, setNotes] = useState({
    id: shot.id,
    rating: 0,
    beanId: '',
    beanType: '',
    doseIn: '',
    doseOut: '',
    ratio: '',
    grinder: '',
    grindSetting: '',
    balanceTaste: 'balanced',
    notes: '',
  });

  const [loading, setLoading] = useState(false);
  const [isEditing, setIsEditing] = useState(false);
  const [initialLoaded, setInitialLoaded] = useState(false);
  const [availableBeans, setAvailableBeans] = useState([]);
  const savedNotesRef = useRef(null);
  const beanFieldListId = `bean-options-${notesKey}`;

  // Calculate ratio function
  const calculateRatio = useCallback((doseIn, doseOut) => {
    if (doseIn && doseOut && parseFloat(doseIn) > 0 && parseFloat(doseOut) > 0) {
      return (parseFloat(doseOut) / parseFloat(doseIn)).toFixed(2);
    }
    return '';
  }, []);

  // Load notes ONLY on component mount
  useEffect(() => {
    if (initialLoaded) return; // Prevent reloading
    notesService.setApiService(apiService);

    const loadNotes = async () => {
      try {
        let loadedNotes = {
          id: notesKey,
          rating: 0,
          beanId: '',
          beanType: shot.beanName || '',
          doseIn: '',
          doseOut: '',
          ratio: '',
          grinder: '',
          grindSetting: '',
          balanceTaste: 'balanced',
          notes: '',
        };
        const savedNotes = await notesService.loadNotes(notesKey, shot.source || 'gaggimate');
        loadedNotes = { ...loadedNotes, ...savedNotes, id: notesKey };

        // Pre-populate doseOut with shot.volume if it's empty and shot.volume exists
        if (!loadedNotes.doseOut && shot.volume) {
          loadedNotes.doseOut = shot.volume.toFixed(1);
        }

        // Calculate ratio from loaded data
        if (loadedNotes.doseIn && loadedNotes.doseOut) {
          loadedNotes.ratio = calculateRatio(loadedNotes.doseIn, loadedNotes.doseOut);
        }

        setNotes(loadedNotes);
        savedNotesRef.current = loadedNotes;
        setInitialLoaded(true);
        // Pass loaded notes to parent
        if (onNotesLoaded) {
          onNotesLoaded(loadedNotes);
        }
      } catch (error) {
        console.error('Failed to load notes:', error);

        // Even if loading fails, set up defaults
        const defaultNotes = {
          id: notesKey,
          rating: 0,
          beanId: '',
          beanType: shot.beanName || '',
          doseIn: '',
          doseOut: shot.volume ? shot.volume.toFixed(1) : '',
          ratio: '',
          grinder: '',
          grindSetting: '',
          balanceTaste: 'balanced',
          notes: '',
        };

        setNotes(defaultNotes);
        savedNotesRef.current = defaultNotes;
        setInitialLoaded(true);
        if (onNotesLoaded) {
          onNotesLoaded(defaultNotes);
        }
      }
    };

    loadNotes();
  }, []); // No dependencies - only run once

  // Reset if shot changes
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
        if (!cancelled) {
          setAvailableBeans(beans.filter(bean => !bean.archived));
        }
      } catch (error) {
        console.error('Failed to load beans for shot notes:', error);
        if (!cancelled) {
          setAvailableBeans([]);
        }
      }
    };

    loadAvailableBeans();

    const handleBeansChanged = () => {
      loadAvailableBeans();
    };

    window.addEventListener('beans-library-changed', handleBeansChanged);
    return () => {
      cancelled = true;
      window.removeEventListener('beans-library-changed', handleBeansChanged);
    };
  }, [apiService]);

  useEffect(() => {
    if (!availableBeans.length || notes.beanId || !notes.beanType) return;
    const matchedBean = availableBeans.find(
      bean =>
        String(bean.name || '')
          .trim()
          .toLowerCase() ===
        String(notes.beanType || '')
          .trim()
          .toLowerCase(),
    );
    if (matchedBean) {
      setNotes(prev => ({ ...prev, beanId: matchedBean.id }));
    }
  }, [availableBeans, notes.beanId, notes.beanType]);

  const saveNotes = async () => {
    setLoading(true);
    try {
      await notesService.saveNotes(notesKey, shot.source || 'gaggimate', notes);
      await syncBeanUsageFromNotes(apiService, savedNotesRef.current, notes);
      savedNotesRef.current = notes;
      setIsEditing(false);
      if (onNotesUpdate) {
        onNotesUpdate(notes);
      }
    } catch (error) {
      console.error('Failed to save notes:', error);
    } finally {
      setLoading(false);
    }
  };

  const handleInputChange = (field, value) => {
    setNotes(prev => {
      const matchedBean =
        field === 'beanType'
          ? availableBeans.find(bean => normalizeBeanName(bean.name) === normalizeBeanName(value))
          : availableBeans.find(bean => bean.id === prev.beanId) || null;
      const newNotes = {
        ...prev,
        [field]: field === 'rating' ? normalizeTenPointRating(value) : value,
      };

      if (field === 'beanType') {
        newNotes.beanId = matchedBean?.id || '';
      }

      // Only recalculate ratio if we're changing doseIn or doseOut
      if ((field === 'doseIn' || field === 'doseOut') && initialLoaded) {
        const doseIn = field === 'doseIn' ? value : prev.doseIn;
        const doseOut = field === 'doseOut' ? value : prev.doseOut;
        newNotes.ratio = calculateRatio(doseIn, doseOut);
      }

      return newNotes;
    });
  };

  const normalizeBeanName = value =>
    String(value || '')
      .trim()
      .toLowerCase();

  const renderStars = rating => (
    <div className='relative inline-flex text-lg leading-none'>
      <div className='text-gray-300'>{'\u2605\u2605\u2605\u2605\u2605'}</div>
      <div
        className='absolute inset-y-0 left-0 overflow-hidden whitespace-nowrap text-yellow-400'
        style={{ width: getRatingFillPercent(rating) }}
      >
        {'\u2605\u2605\u2605\u2605\u2605'}
      </div>
    </div>
  );

  const getTasteColor = taste => {
    switch (taste) {
      case 'bitter':
        return 'text-orange-600';
      case 'sour':
        return 'text-yellow-600';
      case 'balanced':
        return 'text-green-600';
      default:
        return '';
    }
  };

  // Don't render until initial load is complete
  if (!initialLoaded) {
    return (
      <div className='mt-6 border-t pt-6'>
        <div className='flex items-center justify-center py-8'>
          <span className='loading loading-spinner loading-md'></span>
        </div>
      </div>
    );
  }

  return (
    <div className='border-t-base-content/10 accent mt-6 border-t-2 pt-6'>
      <div className='mb-4 flex items-center justify-between'>
        <h3 className='text-lg font-semibold'>Shot Notes</h3>
        {!isEditing ? (
          <button onClick={() => setIsEditing(true)} className='btn btn-sm btn-outline'>
            <FontAwesomeIcon icon={faEdit} />
            Edit
          </button>
        ) : (
          <div className='flex gap-2'>
            <button
              onClick={() => setIsEditing(false)}
              className='btn btn-sm btn-ghost'
              disabled={loading}
            >
              Cancel
            </button>
            <button onClick={saveNotes} className='btn btn-sm btn-primary' disabled={loading}>
              {loading ? (
                <Spinner size={4} />
              ) : (
                <>
                  <FontAwesomeIcon icon={faSave} />
                  Save
                </>
              )}
            </button>
          </div>
        )}
      </div>

      <div className='grid grid-cols-1 gap-6 md:grid-cols-2 xl:grid-cols-4'>
        {/* Rating */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Rating</label>
          <div className='flex items-center gap-3'>
            {renderStars(notes.rating)}
            {isEditing ? (
              <input
                type='number'
                min='0'
                max='10'
                step='0.25'
                className='input input-bordered w-28'
                value={notes.rating || ''}
                onChange={e => handleInputChange('rating', e.target.value)}
                placeholder='0-10'
              />
            ) : (
              <div className='text-sm font-medium'>{formatTenPointRating(notes.rating)}</div>
            )}
          </div>
        </div>

        {/* Bean Type */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Bean Type</label>
          {isEditing ? (
            <>
              <input
                type='text'
                list={beanFieldListId}
                className='input input-bordered w-full'
                value={notes.beanType}
                onChange={e => handleInputChange('beanType', e.target.value)}
                placeholder='Choose a saved bean or enter a custom name'
              />
              <datalist id={beanFieldListId}>
                {availableBeans.map(bean => (
                  <option key={bean.id} value={bean.name} />
                ))}
              </datalist>
            </>
          ) : (
            <div className='input input-bordered bg-base-200 w-full cursor-default'>
              {notes.beanType || '\u2014'}
            </div>
          )}
        </div>

        {/* Dose In */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Dose In (g)</label>
          {isEditing ? (
            <input
              type='number'
              step='0.1'
              className='input input-bordered w-full'
              value={notes.doseIn}
              onChange={e => handleInputChange('doseIn', e.target.value)}
              placeholder='18.0'
            />
          ) : (
            <div className='input input-bordered bg-base-200 w-full cursor-default'>
              {notes.doseIn || '\u2014'}
            </div>
          )}
        </div>

        {/* Dose Out */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Dose Out (g)</label>
          {isEditing ? (
            <input
              type='number'
              step='0.1'
              className='input input-bordered w-full'
              value={notes.doseOut}
              onChange={e => handleInputChange('doseOut', e.target.value)}
              placeholder='36.0'
            />
          ) : (
            <div className='input input-bordered bg-base-200 w-full cursor-default'>
              {notes.doseOut || '\u2014'}
            </div>
          )}
        </div>

        {/* Ratio */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>
            Ratio (1:{notes.ratio || '\u2014'})
          </label>
          <div className='input input-bordered bg-base-200 w-full cursor-default'>
            {notes.ratio ? `1:${notes.ratio}` : '\u2014'}
          </div>
        </div>

        {/* Grinder */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Grinder</label>
          {isEditing ? (
            <input
              type='text'
              className='input input-bordered w-full'
              value={notes.grinder}
              onChange={e => handleInputChange('grinder', e.target.value)}
              placeholder='e.g., Niche Zero'
            />
          ) : (
            <div className='input input-bordered bg-base-200 w-full cursor-default'>
              {notes.grinder || '\u2014'}
            </div>
          )}
        </div>

        {/* Grind Setting */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Grind Setting</label>
          {isEditing ? (
            <input
              type='text'
              className='input input-bordered w-full'
              value={notes.grindSetting}
              onChange={e => handleInputChange('grindSetting', e.target.value)}
              placeholder='e.g., 2.5, Medium-Fine'
            />
          ) : (
            <div className='input input-bordered bg-base-200 w-full cursor-default'>
              {notes.grindSetting || '\u2014'}
            </div>
          )}
        </div>

        {/* Balance/Taste */}
        <div className='form-control'>
          <label className='mb-2 block text-sm font-medium'>Balance/Taste</label>
          {isEditing ? (
            <select
              className='select select-bordered w-full'
              value={notes.balanceTaste}
              onChange={e => handleInputChange('balanceTaste', e.target.value)}
            >
              <option value='bitter'>Bitter</option>
              <option value='balanced'>Balanced</option>
              <option value='sour'>Sour</option>
            </select>
          ) : (
            <div
              className={`input input-bordered bg-base-200 w-full cursor-default capitalize ${getTasteColor(notes.balanceTaste)}`}
            >
              {notes.balanceTaste}
            </div>
          )}
        </div>
      </div>

      {/* Notes Text Area - Full Width */}
      <div className='form-control mt-6'>
        <label className='mb-2 block text-sm font-medium'>
          Notes{' '}
          {isEditing && <span className='text-xs text-gray-500'>({notes.notes.length}/200)</span>}
        </label>
        {isEditing ? (
          <textarea
            className='textarea textarea-bordered w-full'
            rows='4'
            value={notes.notes}
            maxLength={200}
            onChange={e => handleInputChange('notes', e.target.value)}
            placeholder='Tasting notes, brewing observations, etc...'
          />
        ) : (
          <div className='textarea textarea-bordered bg-base-200 min-h-[6rem] w-full cursor-default'>
            {notes.notes || 'No notes added'}
          </div>
        )}
      </div>
    </div>
  );
}
