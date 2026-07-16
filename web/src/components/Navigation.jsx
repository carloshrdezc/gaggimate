import { useEffect, useRef } from 'preact/hooks';
import { useLocation } from 'preact-iso';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faHome } from '@fortawesome/free-solid-svg-icons/faHome';
import { faTemperatureHalf } from '@fortawesome/free-solid-svg-icons/faTemperatureHalf';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faCog } from '@fortawesome/free-solid-svg-icons/faCog';
import { faList } from '@fortawesome/free-solid-svg-icons/faList';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { faTimeline } from '@fortawesome/free-solid-svg-icons/faTimeline';
import { faRotate } from '@fortawesome/free-solid-svg-icons/faRotate';
import { faMagnifyingGlassChart } from '@fortawesome/free-solid-svg-icons/faMagnifyingGlassChart';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';

const NAVIGATION_SECTIONS = [
  {
    id: 'control',
    items: [
      { label: 'Dashboard', link: '/', icon: faHome },
      { label: 'PID Autotune', link: '/pidtune', icon: faTemperatureHalf },
      { label: 'Bluetooth Devices', link: '/scales', icon: faBluetoothB },
      { label: 'Settings', link: '/settings', icon: faCog },
    ],
  },
  {
    id: 'review',
    showDivider: true,
    items: [
      { label: 'Profiles', link: '/profiles', icon: faList },
      { label: 'Beans', link: '/beans', icon: faLeaf },
      { label: 'Shot History', link: '/history', icon: faTimeline },
      { label: 'Shot Analyzer', link: '/analyzer', icon: faMagnifyingGlassChart, isNew: true },
      { label: 'Statistics', link: '/statistics', icon: faChartSimple, isNew: true },
      { label: 'System & Updates', link: '/ota', icon: faRotate },
    ],
  },
];

const _NAV_BASE = import.meta.env.BASE_URL?.replace(/\/$/, '') ?? '';
const FOCUSABLE_SELECTOR = 'a[href], button:not([disabled]), [tabindex]:not([tabindex="-1"])';

function MenuItem({ icon, isNew = false, label, link, onNavigate }) {
  const { path } = useLocation();
  const isActive = path === link;
  const sharedClassName =
    'btn btn-sm relative h-11 w-full justify-start gap-3 rounded-xl border px-3';
  const baseClassName = `${sharedClassName} border-transparent bg-transparent text-base-content/78 hover:border-base-content/12 hover:bg-base-content/6 hover:text-base-content focus-visible:border-primary/30 focus-visible:bg-primary/10 focus-visible:text-base-content focus-visible:outline-none`;
  const activeClassName = `${sharedClassName} border border-primary/20 bg-primary/88 text-primary-content shadow-[0_12px_24px_-16px_rgba(0,0,0,0.9)] hover:bg-primary hover:text-primary-content focus-visible:outline-none`;
  const className = isActive ? activeClassName : baseClassName;

  return (
    <a
      href={_NAV_BASE + link}
      className={className}
      aria-current={isActive ? 'page' : undefined}
      title={label}
      onClick={onNavigate}
    >
      <FontAwesomeIcon icon={icon} />
      <span className='flex items-center gap-2 overflow-hidden whitespace-nowrap'>
        <span className='truncate'>{label}</span>
        {isNew && <span className='text-success shrink-0 text-[0.65rem] font-bold'>NEW</span>}
      </span>
    </a>
  );
}

export function Navigation({ open = false, onClose } = {}) {
  const { path } = useLocation();
  const closeButtonRef = useRef(null);
  const drawerRef = useRef(null);

  useEffect(() => {
    if (!open) return;

    const previousActiveElement = document.activeElement;
    closeButtonRef.current?.focus();

    return () => {
      previousActiveElement?.focus?.();
    };
  }, [open]);

  useEffect(() => {
    if (!open) return;

    const handleKeyDown = event => {
      if (event.key === 'Escape') {
        onClose?.();
        return;
      }

      if (event.key !== 'Tab') {
        return;
      }

      const drawerEl = drawerRef.current;
      if (!drawerEl) {
        return;
      }

      const focusableElements = Array.from(drawerEl.querySelectorAll(FOCUSABLE_SELECTOR)).filter(
        el => !el.closest('[aria-hidden="true"]'),
      );
      const firstElement = focusableElements[0];
      const lastElement = focusableElements[focusableElements.length - 1];

      if (!firstElement || !lastElement) {
        return;
      }

      if (!drawerEl.contains(document.activeElement)) {
        event.preventDefault();
        firstElement.focus();
        return;
      }

      if (event.shiftKey && document.activeElement === firstElement) {
        event.preventDefault();
        lastElement.focus();
        return;
      }

      if (!event.shiftKey && document.activeElement === lastElement) {
        event.preventDefault();
        firstElement.focus();
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [open, onClose]);

  return (
    <>
      <button
        type='button'
        className={`nav-drawer-backdrop ${open ? 'is-open' : ''}`}
        aria-hidden={!open}
        tabIndex={open ? 0 : -1}
        onClick={onClose}
      />
      <aside
        ref={drawerRef}
        id='app-navigation-drawer'
        className={`nav-drawer ${open ? 'is-open' : ''}`}
        role={open ? 'dialog' : undefined}
        aria-modal={open ? 'true' : undefined}
        aria-label='Navigation menu'
        aria-hidden={open ? undefined : true}
      >
        <div className='border-base-300/65 bg-base-100/96 flex h-full max-h-screen flex-col overflow-hidden rounded-r-[1.75rem] border-r p-4 shadow-[0_26px_60px_-24px_rgba(0,0,0,0.92)] backdrop-blur-xl sm:w-[19rem]'>
          <div className='mb-4 flex items-center justify-between gap-3 px-2 pt-1'>
            <div>
              <div className='font-logo text-base-content text-lg tracking-[0.18em]'>GAGGIMATE</div>
              <div className='text-base-content/46 text-[0.65rem] tracking-[0.24em] uppercase'>
                Navigation
              </div>
            </div>
            <button
              ref={closeButtonRef}
              type='button'
              aria-label='Close navigation menu'
              className='btn btn-sm btn-circle text-base-content hover:bg-base-content/10 hover:text-base-content focus-visible:bg-base-content/10 border-none bg-transparent focus-visible:outline-none'
              onClick={onClose}
            >
              <span className='text-lg leading-none'>×</span>
            </button>
          </div>
          <div className='custom-scrollbar min-h-0 flex-1 overflow-y-auto pr-1'>
            {NAVIGATION_SECTIONS.map(section => (
              <div key={section.id}>
                {section.showDivider && <div className='h-3' />}
                <div className='space-y-2'>
                  {section.items.map(item => (
                    <MenuItem key={item.link} {...item} onNavigate={onClose} />
                  ))}
                </div>
              </div>
            ))}
          </div>
        </div>
      </aside>
    </>
  );
}
