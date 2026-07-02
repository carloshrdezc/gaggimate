import { useEffect, useRef } from 'preact/hooks';

const FOCUSABLE_SELECTOR = [
  'a[href]',
  'area[href]',
  'button:not([disabled])',
  'input:not([disabled]):not([type="hidden"])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  'iframe',
  'object',
  'embed',
  '[contenteditable="true"]',
  '[tabindex]:not([tabindex="-1"])',
].join(',');

function isVisible(element) {
  return !!(element.offsetWidth || element.offsetHeight || element.getClientRects().length);
}

function getFocusableElements(container) {
  return Array.from(container.querySelectorAll(FOCUSABLE_SELECTOR)).filter(
    element => !element.hasAttribute('disabled') && !element.getAttribute('aria-hidden') && isVisible(element),
  );
}

function focusElement(element) {
  if (!element) return false;

  try {
    element.focus({ preventScroll: true });
  } catch {
    element.focus();
  }

  return document.activeElement === element;
}

export default function Dialog({
  open,
  onClose,
  title,
  description,
  children,
  initialFocusRef,
  closeOnBackdropClick = false,
  panelClassName = '',
  backdropClassName = 'bg-black/50',
  containerClassName = 'fixed inset-0 z-50 flex items-center justify-center p-4',
}) {
  const dialogRef = useRef(null);
  const previouslyFocusedRef = useRef(null);
  const titleIdRef = useRef(`dialog-title-${Math.random().toString(36).slice(2)}`);
  const descriptionIdRef = useRef(`dialog-description-${Math.random().toString(36).slice(2)}`);

  useEffect(() => {
    if (!open) return undefined;

    const body = document.body;
    const previousOverflow = body.style.overflow;
    const activeElement = document.activeElement;

    previouslyFocusedRef.current = activeElement instanceof HTMLElement ? activeElement : null;
    body.style.overflow = 'hidden';

    const rafId = requestAnimationFrame(() => {
      const container = dialogRef.current;
      if (!container) return;

      if (initialFocusRef?.current && focusElement(initialFocusRef.current)) return;

      const initialTarget = container.querySelector('[data-dialog-initial-focus="true"]');
      if (initialTarget instanceof HTMLElement && focusElement(initialTarget)) return;

      const focusableElements = getFocusableElements(container);
      if (focusableElements.length > 0) {
        focusElement(focusableElements[0]);
        return;
      }

      focusElement(container);
    });

    return () => {
      cancelAnimationFrame(rafId);
      body.style.overflow = previousOverflow;

      const previouslyFocused = previouslyFocusedRef.current;
      if (previouslyFocused && previouslyFocused.isConnected) {
        focusElement(previouslyFocused);
      }
    };
  }, [open, initialFocusRef]);

  if (!open) return null;

  const handleBackdropMouseDown = event => {
    if (!closeOnBackdropClick || event.target !== event.currentTarget) return;
    onClose();
  };

  const handleKeyDown = event => {
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      onClose();
      return;
    }

    if (event.key !== 'Tab') return;

    const container = dialogRef.current;
    if (!container) return;

    const focusableElements = getFocusableElements(container);
    if (!focusableElements.length) {
      event.preventDefault();
      focusElement(container);
      return;
    }

    const activeElement = document.activeElement;
    const currentIndex = focusableElements.indexOf(activeElement);

    if (currentIndex === -1) {
      event.preventDefault();
      focusElement(focusableElements[0]);
      return;
    }

    if (event.shiftKey) {
      if (currentIndex === 0) {
        event.preventDefault();
        focusElement(focusableElements[focusableElements.length - 1]);
      }
      return;
    }

    if (currentIndex === focusableElements.length - 1) {
      event.preventDefault();
      focusElement(focusableElements[0]);
    }
  };

  const labelledBy = title ? titleIdRef.current : undefined;
  const describedBy = description ? descriptionIdRef.current : undefined;

  return (
    <div className={containerClassName} onKeyDown={handleKeyDown}>
      <div className={`absolute inset-0 ${backdropClassName}`} aria-hidden='true' onMouseDown={handleBackdropMouseDown} />
      <div
        ref={dialogRef}
        role='dialog'
        aria-modal='true'
        aria-labelledby={labelledBy}
        aria-describedby={describedBy}
        tabIndex='-1'
        className={`relative z-10 outline-none ${panelClassName}`}
      >
        {title ? (
          <h2 id={titleIdRef.current} className='sr-only'>
            {title}
          </h2>
        ) : null}
        {description ? (
          <p id={descriptionIdRef.current} className='sr-only'>
            {description}
          </p>
        ) : null}
        {children}
      </div>
    </div>
  );
}
