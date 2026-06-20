import { useCallback, useContext, useEffect, useRef, useState } from 'preact/hooks';
import { computed } from '@preact/signals';
import PropTypes from 'prop-types';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faTimes } from '@fortawesome/free-solid-svg-icons/faTimes';
import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { listBeans, recordBeanSelection, parseQuantity } from '../../utils/beanManager.js';
import { MODE_LABELS, formatNumber, StatRow } from '../../utils/homeConstants.jsx';

const DOSE_STORAGE_KEY = 'gaggimate-dose-grams';
const TARGET_WEIGHT_STORAGE_KEY = 'gaggimate-target-weight';
const DEFAULT_DOSE = 18.0;
const DEFAULT_YIELD = 36.0;

const status = computed(() => machine.value.status);

// Temperature stepper popover
function TempPopover({ currentTemp, targetTemp, onChange, onClose }) {
  return (
    <div
      className='nd-card absolute top-full left-0 z-50 mt-2 w-full p-4'
      onClick={e => e.stopPropagation()}
    >
      <div className='mb-4 flex items-center justify-between'>
        <span className='font-nd-mono text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
          Adjust Temperature
        </span>
        <button type='button' className='nd-popover-close' onClick={onClose} aria-label='Close'>
          <FontAwesomeIcon icon={faTimes} />
        </button>
      </div>
      <div className='nd-stepper'>
        <button
          type='button'
          className='nd-stepper-btn'
          onClick={() => onChange(-1)}
          aria-label='Lower temperature'
        >
          −
        </button>
        <span className='nd-stepper-value'>{formatNumber(targetTemp)}°C</span>
        <button
          type='button'
          className='nd-stepper-btn'
          onClick={() => onChange(1)}
          aria-label='Raise temperature'
        >
          +
        </button>
      </div>
      <div className='font-nd-mono mt-3 text-center text-[11px] text-[var(--text-disabled,#666)]'>
        Current: {formatNumber(currentTemp)}°C
      </div>
    </div>
  );
}

TempPopover.propTypes = {
  currentTemp: PropTypes.number,
  targetTemp: PropTypes.number,
  onChange: PropTypes.func.isRequired,
  onClose: PropTypes.func.isRequired,
};

// Profile popover
function ProfilePopover({ profiles, selectedProfileId, onSelect, onClose, loading, error }) {
  return (
    <div
      className='nd-card absolute top-full left-0 z-50 mt-2 w-full p-4'
      onClick={e => e.stopPropagation()}
    >
      <div className='mb-3 flex items-center justify-between'>
        <span className='font-nd-mono text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
          Select Profile
        </span>
        <button type='button' className='nd-popover-close' onClick={onClose} aria-label='Close'>
          <FontAwesomeIcon icon={faTimes} />
        </button>
      </div>
      {loading ? (
        <div className='font-nd-mono py-4 text-center text-[11px] text-[var(--text-disabled,#666)]'>
          [LOADING...]
        </div>
      ) : error ? (
        <div className='font-nd-mono py-4 text-center text-[11px] text-[var(--color-error,#d71921)]'>
          [{error.toUpperCase()}]
        </div>
      ) : (
        <div className='space-y-1 overflow-y-auto' style={{ maxHeight: '240px' }}>
          {profiles.map(profile => (
            <button
              key={profile.id}
              type='button'
              className={`nd-popover-btn w-full px-3 py-2 text-left ${
                profile.id === selectedProfileId ? 'nd-popover-btn--active' : ''
              }`}
              onClick={() => onSelect(profile.id)}
            >
              {profile.label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

ProfilePopover.propTypes = {
  profiles: PropTypes.array,
  selectedProfileId: PropTypes.string,
  onSelect: PropTypes.func.isRequired,
  onClose: PropTypes.func.isRequired,
  loading: PropTypes.bool,
  error: PropTypes.string,
};

// Bean popover
function BeanPopover({ beans, activeBean, onSelect, onClose, loading, error }) {
  return (
    <div
      className='nd-card absolute top-full left-0 z-50 mt-2 w-full p-4'
      onClick={e => e.stopPropagation()}
    >
      <div className='mb-3 flex items-center justify-between'>
        <span className='font-nd-mono text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
          Select Bean
        </span>
        <button type='button' className='nd-popover-close' onClick={onClose} aria-label='Close'>
          <FontAwesomeIcon icon={faTimes} />
        </button>
      </div>
      {loading ? (
        <div className='font-nd-mono py-4 text-center text-[11px] text-[var(--text-disabled,#666)]'>
          [LOADING...]
        </div>
      ) : error ? (
        <div className='font-nd-mono py-4 text-center text-[11px] text-[var(--color-error,#d71921)]'>
          [{error.toUpperCase()}]
        </div>
      ) : (
        <div className='space-y-1 overflow-y-auto' style={{ maxHeight: '240px' }}>
          {beans.map(bean => (
            <button
              key={bean.name}
              type='button'
              className={`nd-popover-btn w-full px-3 py-2 text-left ${
                bean.name === activeBean ? 'nd-popover-btn--active' : ''
              }`}
              onClick={() => onSelect(bean.name)}
            >
              {bean.name}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

BeanPopover.propTypes = {
  beans: PropTypes.array,
  activeBean: PropTypes.string,
  onSelect: PropTypes.func.isRequired,
  onClose: PropTypes.func.isRequired,
  loading: PropTypes.bool,
  error: PropTypes.string,
};

function DoseInput({ value, onAdjust, onInputChange }) {
  const [editing, setEditing] = useState(false);
  const [inputValue, setInputValue] = useState('');
  const inputRef = useRef(null);

  const handleNumberClick = () => {
    setInputValue(String(value));
    setEditing(true);
  };

  const handleInputBlur = () => {
    const parsed = parseQuantity(inputValue);
    if (parsed !== null) {
      onInputChange({ target: { value: String(parsed) } });
    }
    setEditing(false);
  };

  const handleInputKeyDown = e => {
    if (e.key === 'Enter') e.target.blur();
    if (e.key === 'Escape') {
      setInputValue(String(value));
      setEditing(false);
    }
  };

  const handleBlurWithDocumentClick = useCallback(
    e => {
      if (editing && inputRef.current && !inputRef.current.contains(e.target)) {
        handleInputBlur();
      }
    },
    [editing, inputValue, value, onInputChange],
  );

  useEffect(() => {
    document.addEventListener('mousedown', handleBlurWithDocumentClick);
    return () => document.removeEventListener('mousedown', handleBlurWithDocumentClick);
  }, [handleBlurWithDocumentClick]);

  return (
    <div className='nd-stat flex-1'>
      <div className='nd-stat-label'>Dose</div>
      <div className='flex items-center gap-2'>
        <span className='nd-stat-value' onClick={handleNumberClick} style={{ cursor: 'text' }}>
          {value.toFixed(1)}g
        </span>
        <div className='dose-stepper'>
          <button
            type='button'
            className='dose-stepper-btn'
            onClick={e => {
              e.stopPropagation();
              onAdjust(-0.1);
            }}
            aria-label='Decrease dose'
          >
            −
          </button>
          <button
            type='button'
            className='dose-stepper-btn'
            onClick={e => {
              e.stopPropagation();
              onAdjust(0.1);
            }}
            aria-label='Increase dose'
          >
            +
          </button>
        </div>
      </div>
      {editing && (
        <input
          ref={inputRef}
          type='text'
          className='dose-value-input'
          value={inputValue}
          autoFocus
          onBlur={handleInputBlur}
          onKeyDown={handleInputKeyDown}
          onChange={e => setInputValue(e.target.value)}
          onClick={e => e.stopPropagation()}
          style={{
            position: 'absolute',
            left: 0,
            top: '100%',
            zIndex: 10,
            marginTop: '4px',
          }}
        />
      )}
    </div>
  );
}

DoseInput.propTypes = {
  value: PropTypes.number.isRequired,
  onAdjust: PropTypes.func.isRequired,
  onInputChange: PropTypes.func.isRequired,
};

function WeightInput({ value, onAdjust, disabled = false }) {
  const [editing, setEditing] = useState(false);
  const [inputValue, setInputValue] = useState('');
  const inputRef = useRef(null);

  const handleNumberClick = () => {
    if (disabled) return;
    setInputValue(String(value));
    setEditing(true);
  };

  const handleInputBlur = () => {
    const parsed = parseQuantity(inputValue);
    if (!disabled && parsed !== null && parsed > 0) {
      onAdjust(parsed - value);
    }
    setEditing(false);
  };

  const handleInputKeyDown = e => {
    if (e.key === 'Enter') e.target.blur();
    if (e.key === 'Escape') {
      setEditing(false);
    }
  };

  const handleBlurWithDocumentClick = useCallback(
    e => {
      if (editing && inputRef.current && !inputRef.current.contains(e.target)) {
        handleInputBlur();
      }
    },
    [editing, inputValue, value, onAdjust],
  );

  useEffect(() => {
    document.addEventListener('mousedown', handleBlurWithDocumentClick);
    return () => document.removeEventListener('mousedown', handleBlurWithDocumentClick);
  }, [handleBlurWithDocumentClick]);

  return (
    <div className='nd-stat flex-1'>
      <div className='nd-stat-label'>Weight</div>
      <div className='flex items-center gap-2'>
        <span
          className='nd-stat-value'
          onClick={handleNumberClick}
          style={{ cursor: disabled ? 'default' : 'text', opacity: disabled ? 0.5 : 1 }}
        >
          {disabled ? '—' : `${value.toFixed(1)}g`}
        </span>
        {!disabled && (
          <div className='dose-stepper'>
            <button
              type='button'
              className='dose-stepper-btn'
              onClick={e => {
                e.stopPropagation();
                onAdjust(-0.5);
              }}
              aria-label='Decrease weight'
            >
              −
            </button>
            <button
              type='button'
              className='dose-stepper-btn'
              onClick={e => {
                e.stopPropagation();
                onAdjust(0.5);
              }}
              aria-label='Increase weight'
            >
              +
            </button>
          </div>
        )}
      </div>
      {editing && !disabled && (
        <input
          ref={inputRef}
          type='text'
          className='dose-value-input'
          value={inputValue}
          autoFocus
          onBlur={handleInputBlur}
          onKeyDown={handleInputKeyDown}
          onChange={e => setInputValue(e.target.value)}
          onClick={e => e.stopPropagation()}
          style={{
            position: 'absolute',
            left: 0,
            top: '100%',
            zIndex: 10,
            marginTop: '4px',
          }}
        />
      )}
    </div>
  );
}

WeightInput.propTypes = {
  value: PropTypes.number.isRequired,
  onAdjust: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

export default function HomeModeCard({ mode }) {
  const api = useContext(ApiServiceContext);
  const {
    currentTemperature,
    selectedBean,
    selectedProfile,
    selectedProfileId,
    targetPressure,
    targetTemperature,
    brewTargetVolume,
    volumetricAvailable,
    brewTarget,
    allowYieldOverride,
  } = status.value;
  const connected = machine.value.connected;

  const [activePopover, setActivePopover] = useState(null);
  const [profileOptions, setProfileOptions] = useState([]);
  const [beanOptions, setBeanOptions] = useState([]);
  const [loadingProfiles, setLoadingProfiles] = useState(false);
  const [loadingBeans, setLoadingBeans] = useState(false);
  const [profileError, setProfileError] = useState(null);
  const [beanError, setBeanError] = useState(null);

  const [doseGrams, setDoseGrams] = useState(() => {
    const stored = localStorage.getItem(DOSE_STORAGE_KEY);
    const value = parseQuantity(stored) ?? DEFAULT_DOSE;
    if (!stored) localStorage.setItem(DOSE_STORAGE_KEY, String(value));
    return value;
  });

  // Target weight (yield) is DEVICE-AUTHORITATIVE (PRO-221, same class as
  // CAR-371/372/373 and the DashboardMerged CAR-375 treatment). The device
  // owns this value: `tw` in evt:status carries the active profile's total
  // volumetric target (WebUIPlugin -> ProfileManager::getSelectedProfile().
  // getTotalVolume()), surfaced here as `brewTargetVolume`. We seed and reseed
  // from that canonical value so every browser agrees with what the machine
  // will actually brew. localStorage is kept ONLY as an offline fallback: it
  // is read for the initial value when no live device value is available, and
  // it is written on every local change so a disconnected reload has a
  // sensible last-known number — but it must NEVER override a live device
  // value (see the reseed effect below).
  const [targetWeight, setTargetWeight] = useState(() => {
    if (brewTargetVolume > 0) return brewTargetVolume;
    const stored = parseQuantity(localStorage.getItem(TARGET_WEIGHT_STORAGE_KEY));
    return stored ?? DEFAULT_YIELD;
  });

  // Reseed to the device's broadcast target whenever the active profile or the
  // device's volumetric target changes. This makes a change made in one browser
  // appear in another on its next status update, and prevents a stale local
  // value from carrying across a profile switch. When the active profile has no
  // volumetric target (brewTargetVolume <= 0, e.g. a time/pressure profile) the
  // device ignores yield entirely, so we fall back to DEFAULT_YIELD rather than
  // holding a misleading number. localStorage is intentionally NOT consulted
  // here so it can never override a live device value.
  useEffect(() => {
    if (!connected) return;
    setTargetWeight(brewTargetVolume > 0 ? brewTargetVolume : DEFAULT_YIELD);
  }, [connected, selectedProfileId, brewTargetVolume]);

  const loadProfileOptions = useCallback(async () => {
    if (profileOptions.length > 0) return;
    setLoadingProfiles(true);
    setProfileError(null);
    try {
      const response = await api.request({ tp: 'req:profiles:list' });
      const profiles = response?.profiles || [];
      setProfileOptions(profiles.filter(p => !p.archived));
    } catch (err) {
      console.error('Failed to load profiles:', err);
      setProfileError('Failed to load profiles');
    } finally {
      setLoadingProfiles(false);
    }
  }, [api, profileOptions.length]);

  const loadBeanOptions = useCallback(async () => {
    setLoadingBeans(true);
    setBeanError(null);
    try {
      const beans = await listBeans(api);
      setBeanOptions((beans || []).filter(bean => !bean.archived));
    } catch (err) {
      console.error('Failed to load beans:', err);
      setBeanError('Failed to load beans');
    } finally {
      setLoadingBeans(false);
    }
  }, [api]);

  const handleProfileClick = useCallback(() => {
    loadProfileOptions();
    setActivePopover(activePopover === 'profile' ? null : 'profile');
  }, [activePopover, loadProfileOptions]);

  const handleBeanClick = useCallback(() => {
    loadBeanOptions();
    setActivePopover(activePopover === 'bean' ? null : 'bean');
  }, [activePopover, loadBeanOptions]);

  const handleProfileSelect = useCallback(
    async profileId => {
      try {
        await api.request({ tp: 'req:profiles:select', id: profileId });
        setActivePopover(null);
      } catch (err) {
        console.error('Failed to select profile:', err);
      }
    },
    [api],
  );

  const handleBeanSelect = useCallback(
    beanName => {
      try {
        api.send({ tp: 'req:beans:select', name: beanName });
        const selectedBeanObj = beanOptions.find(b => b.name === beanName);
        if (selectedBeanObj) {
          recordBeanSelection({
            profileId: machine.value.status.selectedProfileId,
            profileLabel: machine.value.status.selectedProfile,
            bean: selectedBeanObj,
          });
        }
        setActivePopover(null);
      } catch (err) {
        console.error('Failed to select bean:', err);
      }
    },
    [api, beanOptions],
  );

  const handleTempClick = useCallback(() => {
    setActivePopover(activePopover === 'temp' ? null : 'temp');
  }, [activePopover]);

  const handleTempChange = useCallback(
    delta => {
      try {
        api.send({ tp: delta > 0 ? 'req:raise-temp' : 'req:lower-temp' });
      } catch (error) {
        console.error('Failed to change temperature:', error);
      }
    },
    [api],
  );

  const adjustDose = useCallback(delta => {
    setDoseGrams(prev => {
      const next = Math.round((prev + delta + Number.EPSILON) * 100) / 100;
      const clamped = Math.max(0, next);
      localStorage.setItem(DOSE_STORAGE_KEY, String(clamped));
      return clamped;
    });
  }, []);

  const handleDoseInputChange = useCallback(e => {
    const raw = e.target.value.replace(/g$/, '').trim();
    const parsed = parseQuantity(raw);
    if (parsed !== null) {
      setDoseGrams(parsed);
      localStorage.setItem(DOSE_STORAGE_KEY, String(parsed));
    }
  }, []);

  // Yield is editable only when the device says the active profile is
  // volumetric AND per-shot override is allowed — matching the firmware guards
  // in Controller::setBrewTarget()/raiseBrewTarget() (CAR-375). For
  // non-volumetric profiles the target has no meaning, so we do not let the
  // user push a misleading value to the device.
  const yieldEditable = !!volumetricAvailable && !!brewTarget && !!allowYieldOverride;

  const adjustWeight = useCallback(
    delta => {
      if (!yieldEditable) return;
      const newWeight = Math.round((targetWeight + delta + Number.EPSILON) * 10) / 10;
      const clamped = Math.max(0.5, newWeight);
      setTargetWeight(clamped);
      // Offline fallback cache only — the device remains authoritative and will
      // reseed this value on the next status update once it applies the change.
      try {
        localStorage.setItem(TARGET_WEIGHT_STORAGE_KEY, String(clamped));
      } catch {}
      try {
        api.send({ tp: 'req:change-brew-target', target: clamped });
      } catch (error) {
        console.error('Failed to change weight target:', error);
      }
    },
    [api, targetWeight, yieldEditable],
  );

  const handlePressureChange = useCallback(
    delta => {
      try {
        api.send({ tp: delta > 0 ? 'req:raise-pressure' : 'req:lower-pressure' });
      } catch (error) {
        console.error('Failed to change pressure:', error);
      }
    },
    [api],
  );

  return (
    <div className='flex flex-col gap-4'>
      {/* Connection + Temperature row */}
      <div className='flex gap-3'>
        <div className='nd-stat flex-1'>
          <div className='nd-stat-label'>
            <span
              className={`nd-status-dot mr-2 inline-block align-middle ${connected ? 'nd-status-dot--online' : ''}`}
            />
            Connection
          </div>
          <div className={`nd-stat-value ${connected ? '' : 'text-[var(--warning,#d4a843)]'}`}>
            {connected ? 'Online' : 'Offline'}
          </div>
        </div>
        <div className='nd-stat relative flex-1 cursor-pointer' onClick={handleTempClick}>
          <div className='nd-stat-label'>Temperature</div>
          <div className='nd-stat-value'>
            {formatNumber(currentTemperature)}°C
            <span className='ml-2 text-[11px] text-[var(--text-disabled,#666)]'>
              / {formatNumber(targetTemperature)}°
            </span>
          </div>
          {activePopover === 'temp' && (
            <TempPopover
              currentTemp={currentTemperature}
              targetTemp={targetTemperature}
              onChange={handleTempChange}
              onClose={() => setActivePopover(null)}
            />
          )}
        </div>
      </div>

      {/* Profile / Bean row */}
      <div className='flex gap-3'>
        {/* Left: Profile stacked above Weight */}
        <div className='flex min-w-0 flex-1 flex-shrink-0 flex-col gap-0'>
          <div className='nd-stat relative flex-1 cursor-pointer' onClick={handleProfileClick}>
            <div className='nd-stat-label'>Profile</div>
            <div className='nd-stat-value'>{selectedProfile || 'Default'}</div>
            {activePopover === 'profile' && (
              <ProfilePopover
                profiles={profileOptions}
                selectedProfileId={selectedProfileId}
                onSelect={handleProfileSelect}
                onClose={() => setActivePopover(null)}
                loading={loadingProfiles}
                error={profileError}
              />
            )}
          </div>
          <div className='dose-card'>
            <WeightInput value={targetWeight} onAdjust={adjustWeight} disabled={!yieldEditable} />
          </div>
        </div>

        {/* Right: Bean stacked above Dose */}
        <div className='flex min-w-0 flex-1 flex-shrink-0 flex-col gap-0'>
          <div className='nd-stat relative flex-1 cursor-pointer' onClick={handleBeanClick}>
            <div className='nd-stat-label'>Bean</div>
            <div className='nd-stat-value'>{selectedBean || 'Not selected'}</div>
            {activePopover === 'bean' && (
              <BeanPopover
                beans={beanOptions}
                activeBean={selectedBean}
                onSelect={handleBeanSelect}
                onClose={() => setActivePopover(null)}
                loading={loadingBeans}
                error={beanError}
              />
            )}
          </div>
          <div className='dose-card'>
            <DoseInput
              value={doseGrams}
              onAdjust={adjustDose}
              onInputChange={handleDoseInputChange}
            />
          </div>
        </div>
      </div>

      {/* Shortcuts row */}
      <div className='border-b border-[var(--home-border,#222)] px-5 pt-4 pb-3'>
        <h3 className='font-nd-mono font-400 text-[11px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'>
          Shortcuts
        </h3>
      </div>
      <div className='flex gap-3'>
        <div className='nd-stat relative flex-1 cursor-pointer'>
          <div className='nd-stat-label'>Scales</div>
          <a href='/scales' className='nd-shortcut'>
            [Scales]
          </a>
        </div>

        <div className='nd-stat relative flex-1 cursor-pointer'>
          <div className='nd-stat-label'>History</div>
          <a href='/history' className='nd-shortcut'>
            [History]
          </a>
        </div>
      </div>
      <div className='flex gap-3'>
        <div className='nd-stat relative flex-1 cursor-pointer'>
          <div className='nd-stat-label'>Analyzer</div>
          <a href='/analyzer' className='nd-shortcut'>
            [Analyzer]
          </a>
        </div>

        <div className='nd-stat relative flex-1 cursor-pointer'>
          <div className='nd-stat-label'>Settings</div>
          <a href='/settings' className='nd-shortcut'>
            [Settings]
          </a>
        </div>
      </div>
    </div>
  );
}

HomeModeCard.propTypes = {
  mode: PropTypes.oneOf([0, 1, 2, 3, 4]).isRequired,
};
