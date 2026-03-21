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
    <div className='fixed inset-0 z-[80] flex items-center justify-center px-4 py-6'>
      <button
        type='button'
        className='absolute inset-0 bg-black/45 backdrop-blur-[2px]'
        onClick={onClose}
        aria-label='Close bean selection'
      />
      <div className='relative z-[81] w-full max-w-lg rounded-[1.75rem] border border-base-300/70 bg-base-100/92 p-6 shadow-2xl backdrop-blur-xl'>
        <div className='space-y-2'>
          <div className='text-[0.7rem] font-semibold uppercase tracking-[0.24em] text-base-content/50'>
            Bean Selection
          </div>
          <h3 className='text-2xl font-semibold tracking-tight'>Select a bean for {profile.label}</h3>
          <p className='text-sm leading-relaxed text-base-content/70'>
            This choice will be remembered and used to label future shots in Shot History.
          </p>
        </div>

        <div className='mt-5 space-y-4'>
          <label className='form-control'>
            <span className='mb-2 text-xs font-semibold uppercase tracking-[0.2em] opacity-55'>
              Which bean are you brewing?
            </span>
            <select
              value={selectedBeanId}
              onChange={e => onBeanChange(e.target.value)}
              className='select select-bordered w-full'
            >
              {beans.map(bean => (
                <option key={bean.id} value={bean.id}>
                  {bean.name}
                  {bean.roaster ? ` \u2022 ${bean.roaster}` : ''}
                </option>
              ))}
            </select>
          </label>
        </div>

        <div className='mt-6 flex flex-wrap justify-end gap-2'>
          <button type='button' className='btn btn-ghost btn-sm' onClick={onClose}>
            Cancel
          </button>
          <button type='button' className='btn btn-outline btn-sm' onClick={onSkip}>
            Continue Without Bean
          </button>
          <button type='button' className='btn btn-primary btn-sm' onClick={onConfirm}>
            Select Profile
          </button>
        </div>
      </div>
    </div>
  );
}
