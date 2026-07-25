import PropTypes from 'prop-types';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';

// Shared collapse chevron + click/keyboard wiring for a collapsible header
// (PRO-572). Owns ONLY the chevron rotation/transition and the aria-expanded
// affordance so that behaviour lives in exactly one place; the surrounding
// header layout (title-only for Card, title + enable toggle for a plugin
// sub-card) stays with each caller.
//
// Two shapes:
//  - trigger (default): renders a <button> row that toggles `open` on click.
//    Pass `children` for the visible label/content of that button.
//  - chevron-only (`asChevron`): renders just the rotating chevron button,
//    for headers that already have their own layout row (plugin sub-cards)
//    where the chevron sits next to an unrelated enable toggle.
export function CollapsibleHeader({
  open,
  onToggle,
  title,
  ariaLabel,
  className = '',
  asChevron = false,
  children,
}) {
  const chevron = (
    <FontAwesomeIcon
      icon={faChevronRight}
      className={`text-[10px] transition-transform duration-150 ${open ? 'rotate-90' : ''}`}
    />
  );

  if (asChevron) {
    return (
      <button
        type='button'
        onClick={onToggle}
        aria-expanded={open}
        aria-label={ariaLabel}
        className={`nd-action-btn ${className}`}
        style={{ width: '32px', height: '32px' }}
      >
        {chevron}
      </button>
    );
  }

  return (
    <button
      type='button'
      onClick={onToggle}
      aria-expanded={open}
      aria-label={ariaLabel}
      className={`flex w-full items-center justify-between gap-3 text-left ${className}`}
    >
      {children ?? <span>{title}</span>}
      {chevron}
    </button>
  );
}

CollapsibleHeader.propTypes = {
  open: PropTypes.bool.isRequired,
  onToggle: PropTypes.func.isRequired,
  title: PropTypes.string,
  ariaLabel: PropTypes.string,
  className: PropTypes.string,
  asChevron: PropTypes.bool,
  children: PropTypes.node,
};
