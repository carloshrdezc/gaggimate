import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArchive } from '@fortawesome/free-solid-svg-icons/faArchive';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { faPen } from '@fortawesome/free-solid-svg-icons/faPen';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';

const ROAST_LEVELS = ['Light', 'Medium-Light', 'Medium', 'Medium-Dark', 'Dark'];

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
    <div class="space-y-6">
      {/* Bean Form */}
      <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
        <div class="mb-4 flex items-center gap-2">
          <div class="flex size-8 items-center justify-center rounded-lg" style="background: var(--accent-glow);">
            <FontAwesomeIcon icon={faLeaf} class="text-[--accent]" />
          </div>
          <h2 class="text-sm font-semibold text-[--text-primary]">{editing ? 'Edit Bean' : 'Add New Bean'}</h2>
        </div>

        <div class="grid gap-4 sm:grid-cols-2">
          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Coffee Name *</span>
            <input
              type="text"
              value={draft.name}
              onInput={e => onDraftChange('name', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="Colombia Pink Bourbon"
            />
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Roaster</span>
            <input
              type="text"
              value={draft.roaster}
              onInput={e => onDraftChange('roaster', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="Dak, Sey, Onyx..."
            />
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Roast Level</span>
            <select
              value={draft.roastLevel}
              onChange={e => onDraftChange('roastLevel', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
            >
              <option value="" style="background: var(--bg-base); color: var(--text-primary);">Select roast...</option>
              {ROAST_LEVELS.map(level => (
                <option key={level} value={level} style="background: var(--bg-base); color: var(--text-primary);">{level}</option>
              ))}
            </select>
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Roast Date</span>
            <input
              type="date"
              value={draft.roastDate || ''}
              onInput={e => onDraftChange('roastDate', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none; color-scheme: dark;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
            />
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Origin</span>
            <input
              type="text"
              value={draft.origin || ''}
              onInput={e => onDraftChange('origin', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="Colombia, Ethiopia, Brazil..."
            />
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Process</span>
            <input
              type="text"
              value={draft.process || ''}
              onInput={e => onDraftChange('process', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="Washed, Natural, Honey..."
            />
          </label>

          <label class="block">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Quantity (g)</span>
            <input
              type="number"
              min="0"
              step="0.1"
              value={draft.quantity ?? ''}
              onInput={e => onDraftChange('quantity', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all font-data"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="250"
            />
          </label>

          <label class="block sm:col-span-2">
            <span class="mb-1.5 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">Notes</span>
            <textarea
              value={draft.notes}
              onInput={e => onDraftChange('notes', e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all resize-none"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none; min-height: 80px;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
              placeholder="Tasting notes, brew notes, reminders..."
            />
          </label>
        </div>

        <div class="mt-5 flex items-center justify-between gap-3">
          <p class="text-xs text-[--text-muted]">
            Save beans here so profile selection can ask which coffee you are using.
          </p>
          <div class="flex gap-2">
            {editing && (
              <button
                type="button"
                onClick={onCancel}
                class="px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
              >
                Cancel
              </button>
            )}
            <button
              type="button"
              onClick={onSubmit}
              disabled={busy || !draft.name.trim()}
              class="px-5 py-2 rounded-lg text-sm font-medium text-[--bg-base] transition-all hover:opacity-90 disabled:opacity-50"
              style="background: var(--accent);"
            >
              {editing ? 'Update Bean' : 'Save Bean'}
            </button>
          </div>
        </div>
      </div>

      {/* Bean List */}
      <div class="space-y-2">
        {beans.length === 0 ? (
          <div class="flex flex-col items-center justify-center rounded-xl py-12 text-center" style="border: 1px dashed var(--border);">
            <FontAwesomeIcon icon={faLeaf} class="text-4xl text-[--text-muted] mb-3" />
            <p class="text-[--text-muted] text-sm">No beans saved yet.</p>
            <p class="text-[--text-muted] text-xs mt-1">Add your first bean above.</p>
          </div>
        ) : (
          beans.map(bean => (
            <BeanRow
              key={bean.id}
              bean={bean}
              onEdit={onEdit}
              onDelete={onDelete}
              onArchiveToggle={onArchiveToggle}
              busy={busy}
            />
          ))
        )}
      </div>
    </div>
  );
}

function BeanRow({ bean, onEdit, onDelete, onArchiveToggle, busy }) {
  const details = [bean.roaster, bean.roastLevel].filter(Boolean).join(' · ') || 'Bean details';

  return (
    <div
      class="group rounded-xl p-4 transition-all duration-150 hover:bg-[--bg-elevated]"
      style="border: 1px solid transparent;"
    >
      <div class="flex items-start justify-between gap-3">
        {/* Bean info */}
        <div class="flex items-start gap-3 min-w-0">
          <div class="flex size-9 items-center justify-center rounded-lg shrink-0" style="background: rgba(168,85,247,0.12);">
            <FontAwesomeIcon icon={faLeaf} class="text-sm text-purple-400" />
          </div>
          <div class="min-w-0">
            <div class="flex items-center gap-2 flex-wrap">
              <span class="text-sm font-semibold text-[--text-primary] truncate">{bean.name}</span>
              {bean.archived && (
                <span class="text-xs px-2 py-0.5 rounded-full text-[--warning]" style="background: rgba(234,179,8,0.12);">Archived</span>
              )}
            </div>
            <div class="text-xs text-[--text-muted] mt-0.5">{details}</div>

            {/* Tags */}
            <div class="flex flex-wrap gap-1.5 mt-2">
              {bean.roastDate && (
                <span class="text-xs px-2 py-0.5 rounded-full text-[--text-muted]" style="background: rgba(255,255,255,0.05);">
                  Roast {bean.roastDate}
                </span>
              )}
              {bean.origin && (
                <span class="text-xs px-2 py-0.5 rounded-full text-[--text-muted]" style="background: rgba(255,255,255,0.05);">
                  {bean.origin}
                </span>
              )}
              {bean.process && (
                <span class="text-xs px-2 py-0.5 rounded-full text-[--text-muted]" style="background: rgba(255,255,255,0.05);">
                  {bean.process}
                </span>
              )}
              {bean.quantity !== null && bean.quantity !== undefined && bean.quantity !== '' && (
                <span class="text-xs px-2 py-0.5 rounded-full text-[--text-muted] font-data" style="background: rgba(255,255,255,0.05);">
                  {bean.quantity}g left
                </span>
              )}
            </div>

            {bean.notes && (
              <p class="text-xs text-[--text-secondary] mt-2 leading-relaxed">{bean.notes}</p>
            )}
          </div>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-1 shrink-0 opacity-60 group-hover:opacity-100 transition-opacity">
          <button
            type="button"
            onClick={() => onArchiveToggle(bean)}
            disabled={busy}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--warning] transition-all"
            title={bean.archived ? 'Restore bean' : 'Archive bean'}
          >
            <FontAwesomeIcon icon={faArchive} />
          </button>
          <button
            type="button"
            onClick={() => onEdit(bean)}
            disabled={busy}
            class="btn btn-sm btn-ghost text-[--text-muted] hover:text-[--text-primary] transition-all"
            title="Edit bean"
          >
            <FontAwesomeIcon icon={faPen} />
          </button>
          <button
            type="button"
            onClick={() => onDelete(bean.id)}
            disabled={busy}
            class="btn btn-sm btn-ghost text-[--error] hover:bg-[--error]/10 transition-all"
            title="Delete bean"
          >
            <FontAwesomeIcon icon={faTrashCan} />
          </button>
        </div>
      </div>
    </div>
  );
}