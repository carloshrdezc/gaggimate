import { useState, useCallback, useContext, useMemo, useRef, useEffect } from 'preact/hooks';
import { ApiServiceContext } from '../../services/ApiService.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlay, faPause, faSave } from '@fortawesome/free-solid-svg-icons';
import { Tooltip } from '../../components/Tooltip.jsx';

const STATUS = {
  IDLE: 'idle',
  RUNNING: 'running',
  FINISHED: 'finished',
};

const ManualControls = () => {
  const api = useContext(ApiServiceContext);
  const [status, setStatus] = useState(STATUS.IDLE);
  const [pressure, setPressure] = useState(0.0);
  const [flow, setFlow] = useState(0.0);
  const [temperature, setTemperature] = useState(90.0);
  const [valve, setValve] = useState(1); // 1=open, 0=closed
  const [showSaveModal, setShowSaveModal] = useState(false);
  const [saveLabel, setSaveLabel] = useState('');
  const [saveError, setSaveError] = useState(null);
  const debounceTimerRef = useRef(null);

  // Listen for manual save completion or error
  useEffect(() => {
    const handler = (msg) => {
      if (msg.status === 'error') {
        setSaveError(msg.message || 'Save failed');
        setShowSaveModal(true);
      }
    };
    const id = api.on('manual:error', handler);
    return () => api.off('manual:error', id);
  }, [api]);

  // Sync status when mode changes away from MANUAL (e.g., process ended externally)
  useEffect(() => {
    const handler = (msg) => {
      if (msg.m !== 5 && status === STATUS.RUNNING) {
        setStatus(STATUS.FINISHED);
      }
    };
    const id = api.on('status', handler);
    return () => api.off('status', id);
  }, [status]);

  const sendUpdate = useCallback((updates) => {
    if (debounceTimerRef.current) clearTimeout(debounceTimerRef.current);
    debounceTimerRef.current = setTimeout(() => {
      api.send({ tp: 'req:manual:update', ...updates });
    }, 100);
  }, [api]);

  const handleActivate = useCallback(() => {
    api.send({ tp: 'req:manual:activate' });
    setStatus(STATUS.RUNNING);
  }, [api]);

  const handleDeactivate = useCallback(() => {
    api.send({ tp: 'req:process:deactivate' });
    setStatus(STATUS.FINISHED);
  }, [api]);

  const handleSave = useCallback(() => {
    if (!saveLabel.trim()) return;
    api.send({ tp: 'req:manual:save', label: saveLabel.trim() });
    setShowSaveModal(false);
    setSaveLabel('');
  }, [api, saveLabel]);

  const statusClass = useMemo(() => {
    switch (status) {
      case STATUS.RUNNING:
        return 'bg-warning/20 text-warning border-warning';
      case STATUS.FINISHED:
        return 'bg-success/20 text-success border-success';
      default:
        return 'bg-base-300/50 text-base-content/60 border-base-300';
    }
  }, [status]);

  const statusLabel = useMemo(() => {
    switch (status) {
      case STATUS.RUNNING: return 'Running — Live Control';
      case STATUS.FINISHED: return 'Finished — Save or Discard';
      default: return 'Manual Mode — Press play to begin';
    }
  }, [status]);

  return (
    <div className='flex flex-col items-center gap-4'>
      {/* Status indicator */}
      <div className={`badge badge-lg border-2 font-semibold ${statusClass}`}>
        {statusLabel}
      </div>

      {/* Sliders */}
      <div className='w-full max-w-xs space-y-4'>
        {/* Pressure slider */}
        <div className='form-control'>
          <label className='label'>
            <span className='label-text'>Pressure</span>
            <span className='label-text font-mono'>{pressure.toFixed(1)} bar</span>
          </label>
          <input
            type='range'
            min='0' max='12' step='0.1'
            value={pressure}
            disabled={status !== STATUS.RUNNING}
            onInput={e => {
              setPressure(parseFloat(e.target.value));
              sendUpdate({ pressure: parseFloat(e.target.value) });
            }}
            className='range range-primary'
          />
        </div>

        {/* Flow slider */}
        <div className='form-control'>
          <label className='label'>
            <span className='label-text'>Flow</span>
            <span className='label-text font-mono'>{flow.toFixed(2)} g/s</span>
          </label>
          <input
            type='range'
            min='0' max='5' step='0.01'
            value={flow}
            disabled={status !== STATUS.RUNNING}
            onInput={e => {
              setFlow(parseFloat(e.target.value));
              sendUpdate({ flow: parseFloat(e.target.value) });
            }}
            className='range range-secondary'
          />
        </div>

        {/* Temperature slider */}
        <div className='form-control'>
          <label className='label'>
            <span className='label-text'>Temperature</span>
            <span className='label-text font-mono'>{temperature.toFixed(1)} °C</span>
          </label>
          <input
            type='range'
            min='80' max='110' step='0.5'
            value={temperature}
            disabled={status !== STATUS.RUNNING}
            onInput={e => {
              setTemperature(parseFloat(e.target.value));
              sendUpdate({ temperature: parseFloat(e.target.value) });
            }}
            className='range range-accent'
          />
        </div>

        {/* Valve toggle */}
        <div className='form-control'>
          <label className='label'>
            <span className='label-text'>Valve</span>
            <span className='label-text font-mono'>{valve ? 'Open' : 'Closed'}</span>
          </label>
          <button
            className={`btn btn-sm ${valve ? 'btn-success' : 'btn-error'} gap-2`}
            disabled={status !== STATUS.RUNNING}
            onClick={() => {
              const newValve = valve === 1 ? 0 : 1;
              setValve(newValve);
              sendUpdate({ valve: newValve });
            }}
          >
            {valve ? 'Open' : 'Closed'}
          </button>
        </div>
      </div>

      {/* Action button */}
      <div className='flex flex-col items-center gap-2'>
        {status === STATUS.IDLE && (
          <Tooltip content='Start Manual Brew'>
            <button className='btn btn-circle btn-lg border-2 border-primary bg-primary/10 hover:bg-primary/20 hover:border-primary text-primary' onClick={handleActivate}>
              <FontAwesomeIcon icon={faPlay} className='text-2xl' />
            </button>
          </Tooltip>
        )}
        {status === STATUS.RUNNING && (
          <Tooltip content='Stop'>
            <button className='btn btn-circle btn-lg border-2 border-error bg-error/10 hover:bg-error/20 hover:border-error text-error' onClick={handleDeactivate}>
              <FontAwesomeIcon icon={faPause} className='text-2xl' />
            </button>
          </Tooltip>
        )}
        {status === STATUS.FINISHED && (
          <div className='flex gap-2'>
            <button className='btn btn-primary gap-2' onClick={() => setShowSaveModal(true)}>
              <FontAwesomeIcon icon={faSave} />
              Save Shot
            </button>
            <button className='btn btn-outline' onClick={() => setStatus(STATUS.IDLE)}>
              Discard
            </button>
          </div>
        )}
      </div>

      {/* Save Modal */}
      {showSaveModal && (
        <div className='modal modal-open'>
          <div className='modal-box'>
            <h3 className='font-bold text-lg'>Save Manual Shot</h3>
            {saveError && (
              <div className='alert alert-error mt-3'>
                <span>{saveError}</span>
              </div>
            )}
            <div className='form-control mt-4'>
              <label className='label'>
                <span className='label-text'>Profile Name</span>
              </label>
              <input
                type='text'
                className='input input-bordered'
                placeholder='My Manual Shot'
                value={saveLabel}
                onInput={e => { setSaveLabel(e.target.value); setSaveError(null); }}
              />
            </div>
            <div className='modal-action'>
              <button className='btn btn-ghost' onClick={() => { setShowSaveModal(false); setSaveError(null); }}>Cancel</button>
              <button className='btn btn-primary' onClick={handleSave} disabled={!saveLabel.trim()}>
                Save
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default ManualControls;