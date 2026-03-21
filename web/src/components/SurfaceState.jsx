import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCircleInfo } from '@fortawesome/free-solid-svg-icons/faCircleInfo';
import { faTriangleExclamation } from '@fortawesome/free-solid-svg-icons/faTriangleExclamation';
import { Spinner } from './Spinner.jsx';

const TONE_STYLES = {
  neutral: {
    shell: 'border-base-content/8 bg-base-100/55 text-base-content',
    icon: 'border-base-content/10 bg-base-content/8 text-base-content/70',
  },
  warning: {
    shell: 'border-warning/25 bg-warning/10 text-warning-content',
    icon: 'border-warning/20 bg-warning/15 text-warning-content',
  },
  error: {
    shell: 'border-error/25 bg-error/10 text-error',
    icon: 'border-error/20 bg-error/12 text-error',
  },
};

export function SurfaceState({
  title,
  description,
  loading = false,
  tone = 'neutral',
  icon = faCircleInfo,
  className = '',
  children,
}) {
  const toneStyle = TONE_STYLES[tone] || TONE_STYLES.neutral;
  const resolvedIcon = tone === 'error' ? faTriangleExclamation : icon;

  return (
    <div
      className={`rounded-[1.5rem] border px-6 py-8 shadow-sm backdrop-blur-sm ${toneStyle.shell} ${className}`}
    >
      <div className='mx-auto flex max-w-2xl flex-col items-center text-center'>
        <div
          className={`mb-4 inline-flex h-14 w-14 items-center justify-center rounded-2xl border ${toneStyle.icon}`}
        >
          {loading ? (
            <Spinner size={12} />
          ) : (
            <FontAwesomeIcon icon={resolvedIcon} className='text-xl' />
          )}
        </div>
        {title && <h3 className='text-lg font-semibold tracking-tight sm:text-xl'>{title}</h3>}
        {description && (
          <p className='mt-2 max-w-xl text-sm leading-relaxed opacity-70'>{description}</p>
        )}
        {children && <div className='mt-5 w-full'>{children}</div>}
      </div>
    </div>
  );
}
