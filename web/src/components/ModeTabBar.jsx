import PropTypes from 'prop-types';

const ALL_TABS = [
  { id: 0, label: 'Standby' },
  { id: 1, label: 'Brew' },
  { id: 2, label: 'Steam' },
  { id: 3, label: 'Water' },
  { id: 5, label: 'Manual' },
];

export function ModeTabBar({ mode, changeMode, showGrindTab = false, pressureAvailable = false }) {
  const tabs = showGrindTab ? [...ALL_TABS, { id: 4, label: 'Grind' }] : ALL_TABS;

  return (
    <div className='flex justify-center'>
      <div className='bg-base-300/90 flex w-full max-w-md rounded-xl border border-base-300/65 p-1'>
        {tabs.map(tab => (
          <button
            key={tab.id}
            className={`flex-1 cursor-pointer rounded-lg px-1 py-1.5 text-sm transition-all duration-200 sm:px-4 lg:px-2 lg:py-2 ${
              mode === tab.id
                ? 'bg-primary text-primary-content font-medium shadow-[0_12px_24px_-16px_rgba(0,0,0,0.9)]'
                : 'text-base-content/60 hover:text-base-content'
            }`}
            onClick={() => changeMode(tab.id)}
          >
            {tab.label}
          </button>
        ))}
      </div>
    </div>
  );
}

ModeTabBar.propTypes = {
  mode: PropTypes.oneOf([0, 1, 2, 3, 4, 5]).isRequired,
  changeMode: PropTypes.func.isRequired,
  showGrindTab: PropTypes.bool,
  pressureAvailable: PropTypes.bool,
};
