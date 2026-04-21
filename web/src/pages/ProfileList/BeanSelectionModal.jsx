export function BeanSelectionModal({
  open,
  profile,
  beans,
  selectedBeanId,
  onBeanChange,
  onConfirm,
  onSkip,
  onClose,
}) {
  if (!open || !profile) return null;

  return (
    <div class="fixed inset-0 z-[80] flex items-center justify-center px-4 py-6">
      <button
        type="button"
        class="absolute inset-0 bg-black/45 backdrop-blur-[2px]"
        onClick={onClose}
        aria-label="Close bean selection"
      />
      <div
        class="relative z-[81] w-full max-w-lg rounded-2xl p-6 shadow-2xl"
        style="background: var(--bg-elevated); border: 1px solid var(--border); backdrop-filter: blur(20px);"
      >
        <div class="mb-5 space-y-2">
          <div class="text-xs font-semibold uppercase tracking-widest text-[--text-muted]">
            Bean Selection
          </div>
          <h3 class="text-xl font-semibold text-[--text-primary]">Select a bean for {profile.label}</h3>
          <p class="text-sm leading-relaxed text-[--text-secondary]">
            This choice will be remembered and used to label future shots in Shot History.
          </p>
        </div>

        <div class="space-y-3">
          <label class="block">
            <span class="mb-2 block text-xs font-semibold uppercase tracking-wider text-[--text-muted]">
              Which bean are you brewing?
            </span>
            <select
              value={selectedBeanId}
              onChange={e => onBeanChange(e.target.value)}
              class="w-full px-4 py-2.5 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
              style="background: var(--bg-base); border: 1px solid var(--border); outline: none;"
              onFocus={e => e.target.style.borderColor = 'var(--accent)'}
              onBlur={e => e.target.style.borderColor = 'var(--border)'}
            >
              {beans.map(bean => (
                <option key={bean.id} value={bean.id} style="background: var(--bg-base); color: var(--text-primary);">
                  {bean.name}
                  {bean.roaster ? ` \u2022 ${bean.roaster}` : ''}
                  {bean.quantity !== null && bean.quantity !== undefined ? ` \u2022 ${bean.quantity}g` : ''}
                </option>
              ))}
            </select>
          </label>
        </div>

        <div class="mt-6 flex flex-wrap justify-end gap-2">
          <button
            type="button"
            onClick={onClose}
            class="px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={onSkip}
            class="px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
          >
            Continue Without Bean
          </button>
          <button
            type="button"
            onClick={onConfirm}
            class="px-4 py-2 rounded-lg text-sm font-medium text-[--bg-base] transition-all hover:opacity-90"
            style="background: var(--accent);"
          >
            Select Profile
          </button>
        </div>
      </div>
    </div>
  );
}