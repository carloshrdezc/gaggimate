import PropTypes from 'prop-types';
import { useState } from 'preact/hooks';
import ResizeHandle from '../pages/Home/ResizeHandle.jsx';
import { CollapsibleHeader } from './CollapsibleHeader.jsx';

export default function Card({
  xs,
  sm,
  md,
  lg,
  xl,
  cols,
  rows,
  title,
  children,
  className = '',
  role,
  fullHeight = false,
  onResize,
  resizing = false,
  collapsible = false,
  defaultOpen = false,
  open,
  onToggle,
}) {
  const getGridClasses = () => {
    const breakpoints = [
      { value: xs, prefix: '' },
      { value: sm, prefix: 'sm:' },
      { value: md, prefix: 'md:' },
      { value: lg, prefix: 'lg:' },
      { value: xl, prefix: 'xl:' },
    ];

    return breakpoints
      .filter(bp => bp.value && bp.value >= 1 && bp.value <= 12)
      .map(bp => `${bp.prefix}col-span-${bp.value}`)
      .join(' ');
  };

  const gridClasses = getGridClasses();

  // Controlled when both `open` and `onToggle` are supplied (Settings uses this
  // for Expand All / Collapse All); otherwise self-manage from `defaultOpen`
  // (PRO-572). When `collapsible` is omitted/false the card renders exactly as
  // before for the 15 existing non-Settings call sites.
  const isControlled = open !== undefined && onToggle !== undefined;
  const [internalOpen, setInternalOpen] = useState(defaultOpen);
  const isOpen = isControlled ? open : internalOpen;
  const toggle = isControlled ? onToggle : () => setInternalOpen(o => !o);

  return (
    <div
      className={`nd-card relative overflow-hidden ${gridClasses} ${fullHeight ? 'h-full' : ''} ${resizing ? 'resizing' : ''} ${className}`}
      role={role}
      data-cols={cols}
      data-rows={rows}
    >
      {title &&
        (collapsible ? (
          <div className='nd-card-header border-b border-[var(--home-border,#222)] px-5 py-4'>
            <CollapsibleHeader open={isOpen} onToggle={toggle} ariaLabel={title}>
              <h2 className='font-nd-mono font-400 text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
                {title}
              </h2>
            </CollapsibleHeader>
          </div>
        ) : (
          <div className='nd-card-header border-b border-[var(--home-border,#222)] px-5 py-4'>
            <h2 className='font-nd-mono font-400 text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
              {title}
            </h2>
          </div>
        ))}
      {/* Keep the body mounted even when collapsed and hide it with the `hidden`
          attribute instead of unmounting it (PRO-572). Settings' onSubmit builds
          the save payload from `new FormData(form)`, which only picks up fields
          currently in the DOM; unmounting a collapsed card silently dropped any
          field the user edited before collapsing it. `hidden` keeps every input
          mounted so FormData still captures it regardless of collapse state. */}
      <div
        hidden={collapsible && !isOpen}
        className={`nd-card-body flex flex-col gap-3 p-5 sm:p-6 ${fullHeight ? 'flex-1' : ''}`}
      >
        {children}
      </div>
      {onResize && <ResizeHandle onResizeStart={onResize} />}
    </div>
  );
}

Card.propTypes = {
  xs: PropTypes.number,
  sm: PropTypes.number,
  md: PropTypes.number,
  lg: PropTypes.number,
  xl: PropTypes.number,
  cols: PropTypes.number,
  rows: PropTypes.number,
  title: PropTypes.string,
  children: PropTypes.node,
  className: PropTypes.string,
  role: PropTypes.string,
  fullHeight: PropTypes.bool,
  onResize: PropTypes.func,
  resizing: PropTypes.bool,
  collapsible: PropTypes.bool,
  defaultOpen: PropTypes.bool,
  open: PropTypes.bool,
  onToggle: PropTypes.func,
};
