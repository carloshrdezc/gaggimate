import Card from '../../components/Card.jsx';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArchive } from '@fortawesome/free-solid-svg-icons/faArchive';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { faPen } from '@fortawesome/free-solid-svg-icons/faPen';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';

export function BeanManagerCard({
  beans,
  draft,
  editing,
  onDraftChange,
  onSubmit,
  onEdit,
  onDelete,
  onArchiveToggle,
  onCancel,
  busy,
}) {
  return (
    <Card sm={12} lg={5} title='Beans'>
      <div className='space-y-4'>
        <div className='grid gap-3 sm:grid-cols-2'>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Coffee Name
            </span>
            <input
              type='text'
              value={draft.name}
              onInput={e => onDraftChange('name', e.target.value)}
              className='input input-bordered w-full'
              placeholder='Colombia Pink Bourbon'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Roaster
            </span>
            <input
              type='text'
              value={draft.roaster}
              onInput={e => onDraftChange('roaster', e.target.value)}
              className='input input-bordered w-full'
              placeholder='Dak, Sey, Onyx...'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Roast Level
            </span>
            <input
              type='text'
              value={draft.roastLevel}
              onInput={e => onDraftChange('roastLevel', e.target.value)}
              className='input input-bordered w-full'
              placeholder='Light, Medium...'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Roast Date
            </span>
            <input
              type='date'
              value={draft.roastDate || ''}
              onInput={e => onDraftChange('roastDate', e.target.value)}
              className='input input-bordered w-full'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Origin
            </span>
            <input
              type='text'
              value={draft.origin || ''}
              onInput={e => onDraftChange('origin', e.target.value)}
              className='input input-bordered w-full'
              placeholder='Colombia, Ethiopia, Brazil...'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Process
            </span>
            <input
              type='text'
              value={draft.process || ''}
              onInput={e => onDraftChange('process', e.target.value)}
              className='input input-bordered w-full'
              placeholder='Washed, Natural, Honey...'
            />
          </label>
          <label className='form-control'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Quantity (g)
            </span>
            <input
              type='number'
              min='0'
              step='0.1'
              value={draft.quantity ?? ''}
              onInput={e => onDraftChange('quantity', e.target.value)}
              className='input input-bordered w-full'
              placeholder='250'
            />
          </label>
          <label className='form-control sm:col-span-2'>
            <span className='mb-1 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Notes
            </span>
            <textarea
              value={draft.notes}
              onInput={e => onDraftChange('notes', e.target.value)}
              className='textarea textarea-bordered min-h-24 w-full'
              placeholder='Tasting notes, brew notes, reminders...'
            />
          </label>
        </div>

        <div className='flex flex-wrap items-center justify-between gap-2'>
          <p className='text-sm opacity-65'>
            Save beans here so profile selection can ask which coffee you are using.
          </p>
          <div className='flex gap-2'>
            {editing && (
              <button type='button' onClick={onCancel} className='btn btn-ghost btn-sm'>
                Cancel
              </button>
            )}
            <button type='button' onClick={onSubmit} className='btn btn-primary btn-sm' disabled={busy}>
              {editing ? 'Update Bean' : 'Save Bean'}
            </button>
          </div>
        </div>

        <div className='space-y-2'>
          {beans.length === 0 ? (
            <div className='rounded-2xl border border-dashed border-base-content/10 bg-base-100/35 px-4 py-5 text-sm opacity-65'>
              No beans saved yet.
            </div>
          ) : (
            beans.map(bean => (
              <div
                key={bean.id}
                className='flex flex-col gap-3 rounded-2xl border border-base-content/10 bg-base-100/45 px-4 py-4 shadow-sm'
              >
                <div className='flex items-start justify-between gap-3'>
                  <div className='min-w-0'>
                    <div className='flex items-center gap-2'>
                      <span className='inline-flex size-8 items-center justify-center rounded-xl border border-secondary/15 bg-secondary/10 text-secondary'>
                        <FontAwesomeIcon icon={faLeaf} className='text-sm' />
                      </span>
                      <div className='min-w-0'>
                        <div className='truncate text-sm font-semibold'>{bean.name}</div>
                        <div className='truncate text-xs opacity-60'>
                          {[bean.roaster, bean.roastLevel].filter(Boolean).join(' \u2022 ') ||
                            'Bean details'}
                        </div>
                        <div className='mt-1 flex flex-wrap gap-1.5 text-[0.65rem] font-medium text-base-content/55'>
                          {bean.roastDate && (
                            <span className='rounded-full border border-base-content/10 px-2 py-1'>
                              Roast {bean.roastDate}
                            </span>
                          )}
                          {bean.origin && (
                            <span className='rounded-full border border-base-content/10 px-2 py-1'>
                              {bean.origin}
                            </span>
                          )}
                          {bean.process && (
                            <span className='rounded-full border border-base-content/10 px-2 py-1'>
                              {bean.process}
                            </span>
                          )}
                          {bean.quantity !== null && bean.quantity !== undefined && bean.quantity !== '' && (
                            <span className='rounded-full border border-base-content/10 px-2 py-1'>
                              {bean.quantity}g left
                            </span>
                          )}
                          {bean.archived && (
                            <span className='rounded-full border border-base-content/10 px-2 py-1 text-warning'>
                              Archived
                            </span>
                          )}
                        </div>
                      </div>
                    </div>
                  </div>
                  <div className='flex items-center gap-1'>
                    <button
                      type='button'
                      onClick={() => onArchiveToggle(bean)}
                      className='btn btn-ghost btn-sm btn-square'
                      title={bean.archived ? 'Restore bean' : 'Archive bean'}
                      disabled={busy}
                    >
                      <FontAwesomeIcon icon={faArchive} />
                    </button>
                    <button
                      type='button'
                      onClick={() => onEdit(bean)}
                      className='btn btn-ghost btn-sm btn-square'
                      disabled={busy}
                    >
                      <FontAwesomeIcon icon={faPen} />
                    </button>
                    <button
                      type='button'
                      onClick={() => onDelete(bean.id)}
                      className='btn btn-ghost btn-sm btn-square text-error'
                      disabled={busy}
                    >
                      <FontAwesomeIcon icon={faTrashCan} />
                    </button>
                  </div>
                </div>
                {bean.notes && <p className='text-sm leading-relaxed opacity-70'>{bean.notes}</p>}
              </div>
            ))
          )}
        </div>
      </div>
    </Card>
  );
}
