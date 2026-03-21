import { useLocation } from 'preact-iso';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faHome } from '@fortawesome/free-solid-svg-icons/faHome';
import { faList } from '@fortawesome/free-solid-svg-icons/faList';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { faTimeline } from '@fortawesome/free-solid-svg-icons/faTimeline';
import { faTemperatureHalf } from '@fortawesome/free-solid-svg-icons/faTemperatureHalf';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faCog } from '@fortawesome/free-solid-svg-icons/faCog';
import { faRotate } from '@fortawesome/free-solid-svg-icons/faRotate';
import { faMagnifyingGlassChart } from '@fortawesome/free-solid-svg-icons/faMagnifyingGlassChart';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';

function MenuItem(props) {
  let className =
    'btn btn-sm justify-start gap-3 w-full rounded-full border border-transparent bg-transparent px-3 text-base-content/80 hover:border-base-content/10 hover:bg-base-content/5 hover:text-base-content focus-visible:border-primary/30 focus-visible:bg-primary/10 focus-visible:text-base-content focus-visible:outline-none';
  const { path } = useLocation();
  if (props.active || path === props.link) {
    className =
      'btn btn-sm justify-start gap-3 w-full rounded-full border border-primary/10 bg-primary px-3 text-primary-content hover:bg-primary hover:text-primary-content shadow-sm focus-visible:outline-none';
  }
  return (
    <a href={props.link} className={className}>
      <FontAwesomeIcon icon={props.icon} />
      <span>{props.label}</span>
    </a>
  );
}

export function Navigation(props) {
  return (
    <nav className='hidden lg:col-span-2 lg:block lg:sticky lg:top-28'>
      <div className='max-h-[calc(100vh-8rem)] space-y-3 overflow-y-auto rounded-[1.75rem] border border-base-300/70 bg-base-100/80 p-4 shadow-lg shadow-base-content/5 backdrop-blur-xl'>
        <div className='space-y-2'>
          <div className='px-2 text-[0.65rem] font-semibold uppercase tracking-[0.25em] text-base-content/45'>
            Control
          </div>
          <MenuItem label='Dashboard' link='/' icon={faHome} />
          <MenuItem label='PID Autotune' link='/pidtune' icon={faTemperatureHalf} />
          <MenuItem label='Bluetooth Devices' link='/scales' icon={faBluetoothB} />
          <MenuItem label='Settings' link='/settings' icon={faCog} />
        </div>
        <div className='space-y-2'>
          <div className='px-2 text-[0.65rem] font-semibold uppercase tracking-[0.25em] text-base-content/45'>
            Review
          </div>
          <MenuItem label='Profiles' link='/profiles' icon={faList} />
          <MenuItem label='Beans' link='/beans' icon={faLeaf} />
          <MenuItem label='Shot History' link='/history' icon={faTimeline} />
          <MenuItem label='Shot Analyzer' link='/analyzer' icon={faMagnifyingGlassChart} />
          <MenuItem label='Statistics' link='/statistics' icon={faChartSimple} />
          <MenuItem label='System & Updates' link='/ota' icon={faRotate} />
        </div>
      </div>
    </nav>
  );
}
