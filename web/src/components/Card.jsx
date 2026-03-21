export default function Card({
  xs,
  sm,
  md,
  lg,
  xl,
  title,
  children,
  className = '',
  role,
  fullHeight = false,
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

  return (
    <div
      className={`card overflow-hidden rounded-[1.75rem] border border-base-300/70 bg-base-100/85 shadow-xl shadow-base-content/5 backdrop-blur-sm transition-[transform,box-shadow] duration-200 sm:hover:-translate-y-0.5 sm:hover:shadow-2xl ${gridClasses} ${fullHeight ? 'h-full' : ''} ${className}`}
      role={role}
    >
      {title && (
        <div className='card-header px-5 pt-5'>
          <h2 className='card-title text-lg tracking-tight sm:text-xl'>{title}</h2>
        </div>
      )}
      <div className={`card-body flex flex-col gap-3 p-5 ${fullHeight ? 'flex-1' : ''}`}>
        {children}
      </div>
    </div>
  );
}
