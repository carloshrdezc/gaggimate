import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faMugHot } from '@fortawesome/free-solid-svg-icons/faMugHot';

export function Footer({ publicMode = false } = {}) {
  return (
    <footer id='page-footer' className='flex grow-0 items-center pb-3'>
      <div className='container mx-auto px-4 lg:px-8 xl:max-w-7xl'>
        <div className='border-base-300/70 text-base-content/60 flex flex-col gap-2 rounded-[1.5rem] border bg-base-100/70 px-4 py-3 text-center text-sm font-medium shadow-lg shadow-base-content/5 backdrop-blur md:flex-row md:items-center md:justify-between md:gap-0 md:text-start xl:py-4'>
          <div className='inline-flex items-center justify-center gap-2'>
            <span>{publicMode ? 'Open source espresso tooling, crafted with' : 'Crafted with'}</span>
            <span className='inline-flex size-6 items-center justify-center rounded-full bg-primary/10 text-primary'>
              <FontAwesomeIcon icon={faMugHot} className='text-xs' />
            </span>
            <span>in Italy by</span>
          </div>
          <a
            className='text-primary font-medium transition hover:text-primary/80 focus-visible:text-primary/80 focus-visible:outline-none'
            href='https://gaggimate.eu'
            target='_blank'
            rel='noreferrer'
          >
            Caffinnova S.r.l.
          </a>
        </div>
      </div>
    </footer>
  );
}
