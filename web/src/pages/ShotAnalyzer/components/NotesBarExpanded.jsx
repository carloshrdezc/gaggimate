/**
 * NotesBarExpanded.jsx
 * Expandable panel below NotesBar.
 * View mode: shows notes text, balance/taste, and an edit button.
 */

import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEdit } from '@fortawesome/free-solid-svg-icons/faEdit';
import { faSave } from '@fortawesome/free-solid-svg-icons/faSave';
import { faTimes } from '@fortawesome/free-solid-svg-icons/faTimes';
import { faCircleNotch } from '@fortawesome/free-solid-svg-icons/faCircleNotch';
import { getNotesTasteStyle } from '../utils/analyzerUtils';
import {
  formatTenPointRating,
  getRatingFillPercent,
  normalizeTenPointRating,
} from '../../../utils/ratings';

const tasteOptions = [
  { value: 'bitter', label: 'Bitter' },
  { value: 'balanced', label: 'Balanced' },
  { value: 'sour', label: 'Sour' },
];

export function NotesBarExpanded({
  currentShot,
  notes,
  isEditing,
  saving,
  onInputChange,
  onEdit,
  onSave,
  onCancel,
  onCollapse,
}) {
  const borderClasses = 'border-base-content/5 border-t';

  const labelCls = 'text-base-content/50 text-[10px] font-semibold uppercase tracking-wider';
  const inputCls = 'input input-sm border-base-content/20 bg-base-100 w-full text-sm';
  const getSelectedTasteButtonStyle = taste => {
    const tasteStyle = getNotesTasteStyle(taste);
    if (!tasteStyle) return undefined;
    return {
      color: tasteStyle.color,
      borderColor: tasteStyle.borderColor,
      backgroundColor: tasteStyle.selectedBackground,
    };
  };

  const renderStars = () => (
    <div className='relative inline-flex text-xl leading-none'>
      <div className='text-base-content/20'>{'\u2605\u2605\u2605\u2605\u2605'}</div>
      <div
        className='absolute inset-y-0 left-0 overflow-hidden whitespace-nowrap text-yellow-400'
        style={{ width: getRatingFillPercent(notes.rating) }}
      >
        {'\u2605\u2605\u2605\u2605\u2605'}
      </div>
    </div>
  );

  return (
    <div className={`transition-all duration-200 ${borderClasses}`}>
      <div className='px-4 py-3'>
        {isEditing ? (
          <div className='space-y-4'>
            <div className='grid grid-cols-2 gap-4'>
              <div>
                <div className={`${labelCls} mb-1.5`}>Rating</div>
                <div className='flex items-center gap-3'>
                  {renderStars()}
                  <input
                    type='number'
                    min='0'
                    max='10'
                    step='0.25'
                    className='input input-sm border-base-content/20 bg-base-100 w-24 text-sm'
                    value={notes.rating || ''}
                    onChange={e => onInputChange('rating', normalizeTenPointRating(e.target.value))}
                    placeholder='0-10'
                  />
                </div>
              </div>
              <div>
                <div className={`${labelCls} mb-1.5`}>Balance / Taste</div>
                <div className='flex gap-1'>
                  {tasteOptions.map(opt => (
                    <button
                      key={opt.value}
                      type='button'
                      onClick={() => onInputChange('balanceTaste', opt.value)}
                      className={`flex-1 rounded-md border-2 px-2 py-1.5 text-xs font-medium transition-all ${
                        notes.balanceTaste === opt.value
                          ? ''
                          : 'border-base-content/10 text-base-content/40 hover:border-base-content/30'
                      }`}
                      style={
                        notes.balanceTaste === opt.value
                          ? getSelectedTasteButtonStyle(opt.value)
                          : undefined
                      }
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>
            </div>

            <div className='grid grid-cols-3 gap-3'>
              <div>
                <div className={`${labelCls} mb-1`}>Dose In (g)</div>
                <input
                  type='number'
                  step='0.1'
                  className={inputCls}
                  value={notes.doseIn}
                  onChange={e => onInputChange('doseIn', e.target.value)}
                  placeholder='18.0'
                />
              </div>
              <div>
                <div className={`${labelCls} mb-1`}>Dose Out (g)</div>
                <input
                  type='number'
                  step='0.1'
                  className={inputCls}
                  value={notes.doseOut}
                  onChange={e => onInputChange('doseOut', e.target.value)}
                  placeholder='36.0'
                />
              </div>
              <div>
                <div className={`${labelCls} mb-1`}>Ratio</div>
                <div className='input input-sm border-base-content/10 bg-base-200/50 w-full text-sm'>
                  {notes.ratio ? `1:${notes.ratio}` : '\u2014'}
                </div>
              </div>
            </div>

            <div className='grid grid-cols-3 gap-3'>
              <div>
                <div className={`${labelCls} mb-1`}>Bean Type</div>
                <input
                  type='text'
                  className={inputCls}
                  value={notes.beanType}
                  onChange={e => onInputChange('beanType', e.target.value)}
                  placeholder='e.g., Single Origin, Blend'
                />
              </div>
              <div>
                <div className={`${labelCls} mb-1`}>Grinder</div>
                <input
                  type='text'
                  className={inputCls}
                  value={notes.grinder}
                  onChange={e => onInputChange('grinder', e.target.value)}
                  placeholder='e.g., Niche Zero'
                />
              </div>
              <div>
                <div className={`${labelCls} mb-1`}>Grind Setting</div>
                <input
                  type='text'
                  className={inputCls}
                  value={notes.grindSetting}
                  onChange={e => onInputChange('grindSetting', e.target.value)}
                  placeholder='e.g., 2.5, Medium-Fine'
                />
              </div>
            </div>

            <div>
              <div className='mb-1 flex items-center justify-between'>
                <div className={labelCls}>Notes</div>
                <span className='text-base-content/40 text-xs'>
                  {(notes.notes || '').length}/200
                </span>
              </div>
              <textarea
                className='textarea textarea-bordered textarea-sm w-full text-sm'
                rows='3'
                value={notes.notes}
                maxLength={200}
                onChange={e => onInputChange('notes', e.target.value)}
                placeholder='Tasting notes, brewing observations...'
              />
            </div>

            <div className='flex justify-end gap-2 pt-1'>
              <button className='btn btn-sm btn-ghost' onClick={onCancel} disabled={saving}>
                <FontAwesomeIcon icon={faTimes} className='mr-1' />
                Cancel
              </button>
              <button
                className='btn btn-sm btn-primary'
                onClick={onSave}
                disabled={saving || currentShot?.source === 'temp'}
                title={
                  currentShot?.source === 'temp'
                    ? 'Not available for temporary shots'
                    : 'Save notes'
                }
              >
                {saving ? (
                  <FontAwesomeIcon icon={faCircleNotch} spin className='mr-1' />
                ) : (
                  <FontAwesomeIcon icon={faSave} className='mr-1' />
                )}
                Save
              </button>
            </div>
          </div>
        ) : (
          <div className='flex items-start gap-3'>
            <div className='min-w-0 flex-1 space-y-2'>
              <div className='flex flex-wrap items-center gap-2 text-xs'>
                <span className='rounded-md bg-base-200/60 px-2 py-1 font-medium'>
                  {renderStars()}
                </span>
                <span className='rounded-md bg-base-200/60 px-2 py-1 font-medium'>
                  {formatTenPointRating(notes.rating)}
                </span>
                <span className='rounded-md bg-base-200/60 px-2 py-1 font-medium'>
                  Grinder: {notes.grinder || '\u2014'}
                </span>
                <span className='rounded-md bg-base-200/60 px-2 py-1 font-medium'>
                  Grind: {notes.grindSetting || '\u2014'}
                </span>
              </div>
              <div
                className='bg-base-200/50 hover:bg-base-200/80 min-h-[2rem] min-w-0 cursor-pointer rounded-md px-3 py-2 text-xs'
                onClick={onCollapse}
                title='Click to collapse'
              >
                {notes.notes || 'No notes added'}
              </div>
            </div>
            <button
              className='btn btn-sm btn-outline flex-shrink-0'
              onClick={onEdit}
              title='Edit notes'
            >
              <FontAwesomeIcon icon={faEdit} className='mr-1' />
              Edit
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
