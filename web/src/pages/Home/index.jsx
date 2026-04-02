import { useCallback, useContext, useState, useEffect } from 'preact/hooks';
import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlugCircleBolt } from '@fortawesome/free-solid-svg-icons/faPlugCircleBolt';
import { faSliders } from '@fortawesome/free-solid-svg-icons/faSliders';
import { faBookmark } from '@fortawesome/free-solid-svg-icons/faBookmark';
import { faTemperatureHigh } from '@fortawesome/free-solid-svg-icons/faTemperatureHigh';
import {
  Chart,
  LineController,
  TimeScale,
  LinearScale,
  PointElement,
  LineElement,
  Legend,
  Filler,
} from 'chart.js';
import 'chartjs-adapter-dayjs-4/dist/chartjs-adapter-dayjs-4.esm';
import { OverviewChart } from '../../components/OverviewChart.jsx';
import Card from '../../components/Card.jsx';
import ProcessControls from './ProcessControls.jsx';
import { getDashboardLayout, DASHBOARD_LAYOUTS } from '../../utils/dashboardManager.js';
import {
  getConfiguredMachineOrigin,
  setConfiguredMachineOrigin,
  shouldWarnAboutMixedContent,
} from '../../services/machineEndpoint.js';

Chart.register(LineController, TimeScale, LinearScale, PointElement, LineElement, Filler, Legend);

const MODE_LABELS = ['Standby', 'Brew', 'Steam', 'Water', 'Grind'];

function formatReading(value, suffix) {
  return `${Number.isFinite(value) ? value.toFixed(1) : '0.0'}${suffix}`;
}

function StatPill({ label, value, tone = 'neutral', icon }) {
  const toneClasses = {
    neutral: 'border-base-300/70 bg-base-100/80 text-base-content',
    accent: 'border-primary/15 bg-primary/10 text-primary',
    success: 'border-success/15 bg-success/10 text-success',
    secondary: 'border-secondary/15 bg-secondary/10 text-secondary',
    error: 'border-error/15 bg-error/10 text-error',
    warning: 'border-warning/15 bg-warning/10 text-warning-content',
  };

  return (
    <div
      className={`status-indicator-card w-full min-w-0 rounded-2xl border px-4 py-3 shadow-sm backdrop-blur ${toneClasses[tone]}`}
    >
      <div className='flex items-center gap-2 text-[0.65rem] font-semibold uppercase tracking-[0.25em] opacity-70'>
        <span className='inline-flex size-7 items-center justify-center rounded-xl border border-current/15 bg-current/10'>
          <FontAwesomeIcon icon={icon} className='text-xs' />
        </span>
        <span>{label}</span>
      </div>
      <div className='mt-1 break-words text-lg font-semibold leading-tight sm:text-xl'>{value}</div>
    </div>
  );
}

export function Home() {
  const [dashboardLayout, setDashboardLayout] = useState(DASHBOARD_LAYOUTS.ORDER_FIRST);
  const [machineOrigin, setMachineOriginState] = useState(() => getConfiguredMachineOrigin());
  const apiService = useContext(ApiServiceContext);

  useEffect(() => {
    setDashboardLayout(getDashboardLayout());

    const handleStorageChange = e => {
      if (e.key === 'dashboardLayout') {
        setDashboardLayout(e.newValue || DASHBOARD_LAYOUTS.ORDER_FIRST);
      }
    };

    window.addEventListener('storage', handleStorageChange);

    return () => {
      window.removeEventListener('storage', handleStorageChange);
    };
  }, []);

  const changeMode = useCallback(
    mode => {
      apiService.send({
        tp: 'req:change-mode',
        mode,
      });
    },
    [apiService],
  );

  const mode = machine.value.status.mode;
  const connected = machine.value.connected;
  const currentMode = MODE_LABELS[mode] || 'Unknown';
  const profileLabel = machine.value.status.selectedProfile || 'Default';
  const temp = formatReading(machine.value.status.currentTemperature, '\u00B0C');
  const pressure = formatReading(machine.value.status.currentPressure, ' bar');
  const showMixedContentWarning = shouldWarnAboutMixedContent();

  const promptForMachineOrigin = useCallback(() => {
    const nextValue = window.prompt(
      'Enter the machine URL or host name',
      machineOrigin || 'http://gaggimate.local',
    );

    if (nextValue === null) {
      return;
    }

    const normalized = setConfiguredMachineOrigin(nextValue);
    setMachineOriginState(normalized);
    apiService.reconnect();
  }, [apiService, machineOrigin]);

  const resetMachineOrigin = useCallback(() => {
    setConfiguredMachineOrigin('');
    setMachineOriginState('');
    apiService.reconnect();
  }, [apiService]);

  return (
    <>
      <div className='mb-5 rounded-[2rem] border border-base-300/70 bg-base-100/80 p-5 shadow-lg shadow-base-content/5 backdrop-blur-xl lg:p-6'>
        <div className='flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between'>
          <div className='max-w-2xl space-y-2'>
            <div className='inline-flex rounded-full border border-base-300/70 bg-base-100/80 px-3 py-1 text-[0.65rem] font-semibold uppercase tracking-[0.25em] text-base-content/60'>
              Live dashboard
            </div>
            <h1 className='text-3xl font-bold tracking-tight sm:text-4xl'>Dashboard</h1>
            <p className='max-w-xl text-sm leading-relaxed text-base-content/65 sm:text-base'>
              Keep an eye on the machine state, switch modes, and jump into charts without losing
              context.
            </p>
          </div>
          <div className='grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4'>
            <StatPill
              label='Connection'
              value={connected ? 'Online' : 'Offline'}
              tone={connected ? 'success' : 'warning'}
              icon={faPlugCircleBolt}
            />
            <StatPill label='Mode' value={currentMode} tone='accent' icon={faSliders} />
            <StatPill label='Profile' value={profileLabel} tone='secondary' icon={faBookmark} />
            <StatPill
              label='Temp / Pressure'
              value={`${temp} \u00B7 ${pressure}`}
              tone='error'
              icon={faTemperatureHigh}
            />
          </div>
        </div>
      </div>

      {!connected && (
        <div className='mb-5 rounded-[2rem] border border-warning/30 bg-warning/10 p-5 shadow-lg shadow-base-content/5 backdrop-blur-xl lg:p-6'>
          <div className='flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between'>
            <div className='max-w-3xl space-y-2'>
              <h2 className='text-xl font-semibold'>Machine connection needed</h2>
              <p className='text-sm leading-relaxed text-base-content/70 sm:text-base'>
                This app can only read live data when it knows where the machine lives. Right now
                it is trying {machineOrigin || 'the current site host'}, which is not responding as
                a GaggiMate machine.
              </p>
              {showMixedContentWarning && (
                <p className='text-sm font-medium text-warning-content'>
                  This page is loaded over HTTPS, so browsers will block connections to an
                  `http://` machine URL. Use the machine&apos;s own local web UI or expose it with a
                  secure HTTPS/WSS tunnel first.
                </p>
              )}
            </div>
            <div className='flex flex-wrap items-center gap-2'>
              <button type='button' className='btn btn-primary btn-sm' onClick={promptForMachineOrigin}>
                Set Machine URL
              </button>
              {machineOrigin && (
                <button type='button' className='btn btn-ghost btn-sm' onClick={resetMachineOrigin}>
                  Use Site Host
                </button>
              )}
            </div>
          </div>
        </div>
      )}

      <div className='grid grid-cols-1 gap-4 lg:grid-cols-10 lg:items-stretch landscape:sm:grid-cols-10'>
        <Card
          sm={10}
          lg={4}
          className={`landscape:sm:col-span-5 ${dashboardLayout === DASHBOARD_LAYOUTS.ORDER_FIRST ? 'order-first' : 'order-last'}`}
          title='Process Controls'
        >
          <ProcessControls brew={mode === 1} mode={mode} changeMode={changeMode} />
        </Card>

        <Card
          sm={10}
          lg={6}
          className={`landscape:sm:col-span-5 ${dashboardLayout === DASHBOARD_LAYOUTS.ORDER_FIRST ? 'order-last' : 'order-first'}`}
          title='Temperature & Pressure Chart'
          fullHeight={true}
        >
          <OverviewChart />
        </Card>
      </div>
    </>
  );
}
