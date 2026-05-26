import { useRef } from 'preact/hooks';
import Dialog from '../../components/Dialog';

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
  const beanSelectRef = useRef(null);

  if (!open || !profile) return null;

  return (
    <Dialog
      open={open}
      onClose={onClose}
      title={`Select a bean for ${profile.label}`}
      description={`Select a bean for ${profile.label}. This choice will be remembered and used to label future shots in Shot History.`}
      initialFocusRef={beanSelectRef}
      closeOnBackdropClick
      panelClassName='w-full max-w-lg rounded-[1.75rem] border border-base-300/70 bg-base-100/92 p-6 shadow-2xl backdrop-blur-xl'
      backdropClassName='bg-black/45 backdrop-blur-[2px]'
    >
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
            ref={beanSelectRef}
            value={selectedBeanId}
            onChange={e => onBeanChange(e.target.value)}
            className='select select-bordered w-full'
          >
            {beans.map(bean => (
              <option key={bean.id} value={bean.id}>
                {bean.name}
                {bean.roaster ? ` • ${bean.roaster}` : ''}
                {bean.quantity !== null && bean.quantity !== undefined ? ` • ${bean.quantity}g` : ''}
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
    </Dialog>
  );
}
