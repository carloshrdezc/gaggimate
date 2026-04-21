import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faCog } from '@fortawesome/free-solid-svg-icons/faCog';
import { faWifi } from '@fortawesome/free-solid-svg-icons/faWifi';
import { faThermometerHalf } from '@fortawesome/free-solid-svg-icons/faThermometerHalf';
import { faSlidersH } from '@fortawesome/free-solid-svg-icons/faSlidersH';
import { faDesktop } from '@fortawesome/free-solid-svg-icons/faDesktop';
import { faSun } from '@fortawesome/free-solid-svg-icons/faSun';
import { faPalette } from '@fortawesome/free-solid-svg-icons/faPalette';
import { faPlug } from '@fortawesome/free-solid-svg-icons/faPlug';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { computed } from '@preact/signals';
import { useQuery } from 'preact-fetching';
import { useCallback, useEffect, useRef, useState, useContext } from 'preact/hooks';
import { Spinner } from '../../components/Spinner.jsx';
import { timezones } from '../../config/zones.js';
import { machine, ApiServiceContext } from '../../services/ApiService.js';
import { DASHBOARD_LAYOUTS, setDashboardLayout } from '../../utils/dashboardManager.js';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import { getStoredTheme, handleThemeChange } from '../../utils/themeManager.js';
import { PluginCard } from './PluginCard.jsx';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { faEyeSlash } from '@fortawesome/free-solid-svg-icons/faEyeSlash';
import { GoogleDriveBackupCard } from './GoogleDriveBackupCard.jsx';

const ledControl = computed(() => machine.value.capabilities.ledControl);
const pressureAvailable = computed(() => machine.value.capabilities.pressure);

const SectionHeader = ({ icon, title }) => (
  <div class="flex items-center gap-3 mb-4 pb-3" style="border-bottom: 1px solid var(--border);">
    <div class="size-8 rounded-lg flex items-center justify-center" style="background: var(--bg-glass);">
      <FontAwesomeIcon icon={icon} class="text-sm text-[--accent]" />
    </div>
    <h2 class="text-base font-semibold text-[--text-primary]">{title}</h2>
  </div>
);

const FieldLabel = ({ children }) => (
  <label class="block text-xs font-medium text-[--text-secondary] mb-1.5">{children}</label>
);

const FieldHint = ({ children }) => (
  <p class="text-xs text-[--text-muted] mb-2">{children}</p>
);

const DarkInput = ({ id, name, type = 'text', value, placeholder, step, min, max, onChange, disabled }) => (
  <input
    id={id}
    name={name}
    type={type}
    value={value}
    placeholder={placeholder}
    step={step}
    min={min}
    max={max}
    disabled={disabled}
    onChange={onChange}
    class="w-full px-3 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
    style={`background: var(--bg-elevated); border: 1px solid ${disabled ? 'var(--border)' : 'var(--border)'}; outline: none; ${disabled ? 'opacity: 0.5; cursor: not-allowed;' : ''}`}
    onFocus={e => !disabled && (e.target.style.borderColor = 'var(--accent)')}
    onBlur={e => !disabled && (e.target.style.borderColor = 'var(--border)')}
  />
);

const DarkSelect = ({ id, name, value, onChange, children, disabled }) => (
  <select
    id={id}
    name={name}
    value={value}
    disabled={disabled}
    onChange={onChange}
    class="w-full px-3 py-2.5 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
    style={`background: var(--bg-elevated); border: 1px solid var(--border); outline: none; ${disabled ? 'opacity: 0.5; cursor: not-allowed;' : ''}`}
    onFocus={e => e.target.style.borderColor = 'var(--accent)'}
    onBlur={e => e.target.style.borderColor = 'var(--border)'}
  >
    {children}
  </select>
);

const DarkCheckbox = ({ id, name, checked, onChange, label }) => (
  <label class="flex items-center gap-3 cursor-pointer group">
    <div
      class={`relative size-9 rounded-lg flex items-center justify-center transition-all ${checked ? '' : ''}`}
      style="background: var(--bg-elevated); border: 1px solid var(--border);"
      onClick={() => onChange({ currentTarget: { name, value: !checked } })}
    >
      <div
        class="size-4 rounded transition-all"
        style={`background: ${checked ? 'var(--accent)' : 'transparent'};`}
      >
        {checked && (
          <svg viewBox="0 0 12 12" class="w-full h-full">
            <path d="M2 6l3 3 5-5" stroke="var(--bg-base)" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round" />
          </svg>
        )}
      </div>
    </div>
    <span class="text-sm text-[--text-secondary] group-hover:text-[--text-primary] transition-colors">{label}</span>
  </label>
);

export function Settings() {
  const apiService = useContext(ApiServiceContext);
  const [submitting, setSubmitting] = useState(false);
  const [gen, setGen] = useState(0);
  const [formData, setFormData] = useState({});
  const [currentTheme, setCurrentTheme] = useState('dark');
  const [showWifiPassword, setShowWifiPassword] = useState(false);
  const [autowakeupSchedules, setAutoWakeupSchedules] = useState([
    { time: '07:00', days: [true, true, true, true, true, true, true] },
  ]);
  const { isLoading, data: fetchedSettings } = useQuery(`settings/${gen}`, async () => {
    const response = await fetch(`/api/settings`);
    const data = await response.json();
    return data;
  });

  const formRef = useRef();

  useEffect(() => {
    if (fetchedSettings) {
      const settingsWithToggle = {
        ...fetchedSettings,
        standbyDisplayEnabled: fetchedSettings.standbyDisplayEnabled !== undefined
          ? fetchedSettings.standbyDisplayEnabled
          : fetchedSettings.standbyBrightness > 0,
        dashboardLayout: fetchedSettings.dashboardLayout || DASHBOARD_LAYOUTS.ORDER_FIRST,
      };

      if (fetchedSettings.pid) {
        const pidParts = fetchedSettings.pid.split(',');
        if (pidParts.length >= 4) {
          settingsWithToggle.pid = pidParts.slice(0, 3).join(',');
          settingsWithToggle.kf = pidParts[3];
        } else {
          settingsWithToggle.kf = '0.000';
        }
      }

      if (fetchedSettings.autowakeupSchedules) {
        const schedules = [];
        if (typeof fetchedSettings.autowakeupSchedules === 'string' && fetchedSettings.autowakeupSchedules.trim()) {
          const scheduleStrings = fetchedSettings.autowakeupSchedules.split(';');
          for (const scheduleStr of scheduleStrings) {
            const [time, daysStr] = scheduleStr.split('|');
            if (time && daysStr && daysStr.length === 7) {
              const days = daysStr.split('').map(d => d === '1');
              schedules.push({ time, days });
            }
          }
        }
        if (schedules.length === 0) {
          schedules.push({ time: '07:00', days: [true, true, true, true, true, true, true] });
        }
        setAutoWakeupSchedules(schedules);
      } else {
        setAutoWakeupSchedules([{ time: '07:00', days: [true, true, true, true, true, true, true] }]);
      }

      setFormData(settingsWithToggle);
    } else {
      setFormData({});
      setAutoWakeupSchedules([{ time: '07:00', days: [true, true, true, true, true, true, true] }]);
    }
  }, [fetchedSettings]);

  useEffect(() => { setCurrentTheme(getStoredTheme()); }, []);

  const onChange = key => {
    return e => {
      const value = e.currentTarget.type === 'checkbox' ? e.currentTarget.checked : e.currentTarget.value;
      if (key === 'homekit') {
        setFormData(prev => ({ ...prev, homekit: !prev.homekit }));
      } else if (key === 'boilerFillActive') {
        setFormData(prev => ({ ...prev, boilerFillActive: !prev.boilerFillActive }));
      } else if (key === 'smartGrindActive') {
        setFormData(prev => ({ ...prev, smartGrindActive: !prev.smartGrindActive }));
      } else if (key === 'smartGrindToggle') {
        setFormData(prev => ({ ...prev, smartGrindToggle: !prev.smartGrindToggle }));
      } else if (key === 'homeAssistant') {
        setFormData(prev => ({ ...prev, homeAssistant: !prev.homeAssistant }));
      } else if (key === 'momentaryButtons') {
        setFormData(prev => ({ ...prev, momentaryButtons: !prev.momentaryButtons }));
      } else if (key === 'delayAdjust') {
        setFormData(prev => ({ ...prev, delayAdjust: !prev.delayAdjust }));
      } else if (key === 'clock24hFormat') {
        setFormData(prev => ({ ...prev, clock24hFormat: !prev.clock24hFormat }));
      } else if (key === 'autowakeupEnabled') {
        setFormData(prev => ({ ...prev, autowakeupEnabled: !prev.autowakeupEnabled }));
      } else if (key === 'standbyDisplayEnabled') {
        setFormData(prev => {
          const newFormData = { ...prev, standbyDisplayEnabled: !prev.standbyDisplayEnabled };
          if (newFormData.standbyDisplayEnabled === false) newFormData.standbyBrightness = 0;
          return newFormData;
        });
      } else if (key === 'dashboardLayout') {
        setDashboardLayout(value);
        setFormData(prev => ({ ...prev, dashboardLayout: value }));
      } else {
        setFormData(prev => ({ ...prev, [key]: value }));
      }
    };
  };

  const addAutoWakeupSchedule = () => {
    setAutoWakeupSchedules([...autowakeupSchedules, { time: '07:00', days: [true, true, true, true, true, true, true] }]);
  };

  const removeAutoWakeupSchedule = index => {
    if (autowakeupSchedules.length > 1) {
      const newSchedules = autowakeupSchedules.filter((_, i) => i !== index);
      setAutoWakeupSchedules(newSchedules);
    }
  };

  const updateAutoWakeupTime = (index, value) => {
    const newSchedules = [...autowakeupSchedules];
    newSchedules[index].time = value;
    setAutoWakeupSchedules(newSchedules);
  };

  const updateAutoWakeupDay = (scheduleIndex, dayIndex, enabled) => {
    const newSchedules = [...autowakeupSchedules];
    newSchedules[scheduleIndex].days[dayIndex] = enabled;
    setAutoWakeupSchedules(newSchedules);
  };

  const onSubmit = useCallback(async (e, restart = false) => {
    e.preventDefault();
    setSubmitting(true);
    try {
      const form = formRef.current;
      const formDataToSubmit = new FormData(form);
      formDataToSubmit.set('steamPumpPercentage', formData.steamPumpPercentage);
      formDataToSubmit.set('altRelayFunction', formData.altRelayFunction !== undefined ? formData.altRelayFunction : 1);

      if (formData.pid && formData.kf !== undefined) {
        formDataToSubmit.set('pid', `${formData.pid},${formData.kf}`);
      }

      const schedulesStr = autowakeupSchedules
        .map(schedule => `${schedule.time}|${schedule.days.map(d => d ? '1' : '0').join('')}`)
        .join(';');
      formDataToSubmit.set('autowakeupSchedules', schedulesStr);

      if (!formData.standbyDisplayEnabled) {
        formDataToSubmit.set('standbyBrightness', '0');
      }

      if (restart) formDataToSubmit.append('restart', '1');

      const response = await fetch(form.action, { method: 'post', body: formDataToSubmit });
      if (!response.ok) throw new Error(`Server error: ${response.status}`);
      const data = await response.json();

      const updatedData = {
        ...data,
        standbyDisplayEnabled: data.standbyBrightness > 0 ? formData.standbyDisplayEnabled : false,
      };
      setFormData(updatedData);
    } catch (error) {
      console.error('Failed to save settings:', error);
      alert('Failed to save settings. Please try again.');
    } finally {
      setSubmitting(false);
    }
  }, [setFormData, formRef, formData, autowakeupSchedules]);

  const onExport = useCallback(() => {
    const download = prepareDownload('settings.json');
    try {
      downloadJson(formData, 'settings.json', download);
    } catch (error) {
      download.fail(error);
      console.error('Failed to export settings:', error);
    }
  }, [formData]);

  const onUpload = function (evt) {
    if (evt.target.files.length) {
      const file = evt.target.files[0];
      const reader = new FileReader();
      reader.onload = async e => {
        try {
          const data = JSON.parse(e.target.result);
          setFormData(data);
        } catch (error) {
          alert('Failed to parse settings file. Please ensure it is valid JSON.');
        }
      };
      reader.readAsText(file);
    }
  };

  if (isLoading) {
    return (
      <div class="space-y-3">
        {[1, 2, 3].map(i => (
          <div key={i} class="h-48 rounded-xl skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        ))}
      </div>
    );
  }

  return (
    <div class="space-y-6">
      {/* Page header */}
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-2xl font-semibold text-[--text-primary]">Settings</h1>
          <p class="text-sm text-[--text-secondary] mt-1">Configure your GaggiMate preferences</p>
        </div>
        <div class="flex items-center gap-2">
          <button
            type="button"
            onClick={onExport}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--accent] hover:text-[--accent] transition-all"
          >
            <FontAwesomeIcon icon={faFileExport} />
            Export
          </button>
          <label class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--accent] hover:text-[--accent] transition-all cursor-pointer">
            <FontAwesomeIcon icon={faFileImport} />
            Import
            <input onChange={onUpload} class="hidden" type="file" accept=".json,application/json" />
          </label>
        </div>
      </div>

      <form key="settings" ref={formRef} method="post" action="/api/settings" onSubmit={onSubmit}>
        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">

          {/* Temperature Settings */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faThermometerHalf} title="Temperature Settings" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="targetSteamTemp">Default Steam Temperature</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="targetSteamTemp" name="targetSteamTemp" type="number" value={formData.targetSteamTemp} placeholder="135" onChange={onChange('targetSteamTemp')} />
                  <span class="text-sm text-[--text-muted] shrink-0">°C</span>
                </div>
              </div>
              <div>
                <FieldLabel htmlFor="targetWaterTemp">Default Water Temperature</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="targetWaterTemp" name="targetWaterTemp" type="number" value={formData.targetWaterTemp} placeholder="80" onChange={onChange('targetWaterTemp')} />
                  <span class="text-sm text-[--text-muted] shrink-0">°C</span>
                </div>
              </div>
            </div>
          </div>

          {/* User Preferences */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faSlidersH} title="User Preferences" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="startup-mode">Startup Mode</FieldLabel>
                <DarkSelect id="startup-mode" name="startupMode" value={formData.startupMode} onChange={onChange('startupMode')}>
                  <option value="standby" selected={formData.startupMode === 'standby'}>Standby</option>
                  <option value="brew" selected={formData.startupMode === 'brew'}>Brew</option>
                </DarkSelect>
              </div>
              <div>
                <FieldLabel htmlFor="standbyTimeout">Standby Timeout</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="standbyTimeout" name="standbyTimeout" type="number" value={formData.standbyTimeout} placeholder="0" onChange={onChange('standbyTimeout')} />
                  <span class="text-sm text-[--text-muted] shrink-0">s</span>
                </div>
              </div>

              <div class="pt-2" style="border-top: 1px solid var(--border);">
                <FieldLabel> Predictive Scale Delay</FieldLabel>
                <FieldHint>Shuts off the process ahead of time based on flow rate to account for dripping or delays.</FieldHint>
                <div class="mb-3">
                  <DarkCheckbox id="delayAdjust" name="delayAdjust" checked={!!formData.delayAdjust} onChange={onChange('delayAdjust')} label="Auto Adjust" />
                </div>
                <div class="grid grid-cols-2 gap-3">
                  <div>
                    <FieldLabel htmlFor="brewDelay">Brew</FieldLabel>
                    <div class="flex items-center gap-2">
                      <DarkInput id="brewDelay" name="brewDelay" type="number" step="any" value={formData.brewDelay} placeholder="0" onChange={onChange('brewDelay')} />
                      <span class="text-xs text-[--text-muted] shrink-0">ms</span>
                    </div>
                  </div>
                  <div>
                    <FieldLabel htmlFor="grindDelay">Grind</FieldLabel>
                    <div class="flex items-center gap-2">
                      <DarkInput id="grindDelay" name="grindDelay" type="number" step="any" value={formData.grindDelay} placeholder="0" onChange={onChange('grindDelay')} />
                      <span class="text-xs text-[--text-muted] shrink-0">ms</span>
                    </div>
                  </div>
                </div>
              </div>

              <div class="pt-2" style="border-top: 1px solid var(--border);">
                <FieldLabel htmlFor="flushDuration">Flush Duration</FieldLabel>
                <FieldHint>Maximum duration for flushing. (1-60s)</FieldHint>
                <div class="flex items-center gap-2">
                  <DarkInput id="flushDuration" name="flushDuration" type="number" min="1" max="60" value={formData.flushDuration} placeholder="5"
                    onBlur={e => {
                      const val = parseInt(e.target.value) || 5;
                      const clamped = Math.min(60, Math.max(1, val));
                      setFormData(prev => ({ ...prev, flushDuration: clamped }));
                    }}
                    onChange={onChange('flushDuration')}
                  />
                  <span class="text-sm text-[--text-muted] shrink-0">s</span>
                </div>
              </div>

              <DarkCheckbox id="momentaryButtons" name="momentaryButtons" checked={!!formData.momentaryButtons} onChange={onChange('momentaryButtons')} label="Use momentary switches" />
            </div>
          </div>

          {/* Web Settings */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faDesktop} title="Web Settings" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="webui-theme">Theme</FieldLabel>
                <DarkSelect id="webui-theme" name="webui-theme" value={currentTheme} onChange={e => { setCurrentTheme(e.target.value); handleThemeChange(e); }}>
                  <option value="light">Light</option>
                  <option value="dark">Dark</option>
                  <option value="coffee">Coffee</option>
                  <option value="nord">Nord</option>
                  <option value="amoled">AMOLED</option>
                  <option value="stealth">Stealth</option>
                  <option value="crisp">Crisp</option>
                </DarkSelect>
              </div>
              <div>
                <FieldLabel htmlFor="dashboardLayout">Dashboard Layout</FieldLabel>
                <DarkSelect id="dashboardLayout" name="dashboardLayout" value={formData.dashboardLayout || DASHBOARD_LAYOUTS.ORDER_FIRST} onChange={e => { setFormData({ ...formData, dashboardLayout: e.target.value }); setDashboardLayout(e.target.value); }}>
                  <option value={DASHBOARD_LAYOUTS.ORDER_FIRST}>Process Controls First</option>
                  <option value={DASHBOARD_LAYOUTS.ORDER_LAST}>Chart First</option>
                </DarkSelect>
              </div>
            </div>
          </div>

          {/* System Preferences */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faWifi} title="System Preferences" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="wifiSsid">Wi-Fi SSID</FieldLabel>
                <DarkInput id="wifiSsid" name="wifiSsid" type="text" value={formData.wifiSsid} placeholder="Wi-Fi SSID" onChange={onChange('wifiSsid')} />
              </div>
              <div>
                <FieldLabel htmlFor="wifiPassword">Wi-Fi Password</FieldLabel>
                <div class="flex items-center gap-2">
                  <div class="relative flex-grow">
                    <DarkInput id="wifiPassword" name="wifiPassword" type={showWifiPassword ? 'text' : 'password'} value={formData.wifiPassword} placeholder="Wi-Fi Password" onChange={onChange('wifiPassword')} />
                  </div>
                  <button
                    type="button"
                    onClick={() => setShowWifiPassword(!showWifiPassword)}
                    class="shrink-0 p-2.5 rounded-lg text-[--text-muted] hover:text-[--text-primary] transition-all"
                    style="background: var(--bg-base); border: 1px solid var(--border);"
                    aria-label={showWifiPassword ? 'Hide password' : 'Show password'}
                  >
                    <FontAwesomeIcon icon={showWifiPassword ? faEyeSlash : faEye} />
                  </button>
                </div>
              </div>
              <div>
                <FieldLabel htmlFor="mdnsName">Hostname</FieldLabel>
                <DarkInput id="mdnsName" name="mdnsName" type="text" value={formData.mdnsName} placeholder="Hostname" onChange={onChange('mdnsName')} />
              </div>
              <div>
                <FieldLabel htmlFor="timezone">Time Zone</FieldLabel>
                <DarkSelect id="timezone" name="timezone" value={formData.timezone} onChange={onChange('timezone')}>
                  {timezones.map(tz => <option key={tz} value={tz} selected={formData.timezone === tz}>{tz}</option>)}
                </DarkSelect>
              </div>
              <div class="pt-2" style="border-top: 1px solid var(--border);">
                <DarkCheckbox id="clock24hFormat" name="clock24hFormat" checked={!!formData.clock24hFormat} onChange={onChange('clock24hFormat')} label="Use 24h Format" />
              </div>
            </div>
          </div>

          {/* Machine Settings */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faCog} title="Machine Settings" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="pid">PID Values</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="pid" name="pid" type="text" value={formData.pid} placeholder="2.0, 0.1, 0.01" onChange={onChange('pid')} />
                  <span class="text-xs text-[--text-muted] shrink-0">Kp, Ki, Kd</span>
                </div>
              </div>
              <div>
                <FieldLabel htmlFor="kf">Thermal Feedforward Gain (Kff)</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="kf" name="kf" type="number" step="0.001" value={formData.kf} placeholder="0.600" onChange={onChange('kf')} />
                </div>
                <FieldHint>Set to 0 to disable feedforward control.</FieldHint>
              </div>
              <div>
                <FieldLabel htmlFor="pumpModelCoeffs">Pump Flow Coefficients</FieldLabel>
                <FieldHint>Enter 2 values (flow at 1 bar, flow at 9 bar)</FieldHint>
                <DarkInput id="pumpModelCoeffs" name="pumpModelCoeffs" type="text" value={formData.pumpModelCoeffs} placeholder="10.205,5.521" onChange={onChange('pumpModelCoeffs')} />
              </div>
              <div>
                <FieldLabel htmlFor="temperatureOffset">Temperature Offset</FieldLabel>
                <div class="flex items-center gap-2">
                  <DarkInput id="temperatureOffset" name="temperatureOffset" type="number" step="any" value={formData.temperatureOffset} placeholder="0" onChange={onChange('temperatureOffset')} />
                  <span class="text-sm text-[--text-muted] shrink-0">°C</span>
                </div>
              </div>
              {pressureAvailable.value && (
                <div>
                  <FieldLabel htmlFor="pressureScaling">Pressure Sensor Rating</FieldLabel>
                  <FieldHint>Enter the bar rating of the pressure sensor being used</FieldHint>
                  <div class="flex items-center gap-2">
                    <DarkInput id="pressureScaling" name="pressureScaling" type="number" step="any" value={formData.pressureScaling} placeholder="0.0" onChange={onChange('pressureScaling')} />
                    <span class="text-sm text-[--text-muted] shrink-0">bar</span>
                  </div>
                </div>
              )}
              <div>
                <FieldLabel htmlFor="steamPumpPercentage">Steam Pump Assist</FieldLabel>
                <FieldHint>{pressureAvailable.value ? 'How many ml/s to pump into the boiler during steaming' : 'What percentage to run the pump at during steaming'}</FieldHint>
                <div class="flex items-center gap-2">
                  <DarkInput
                    id="steamPumpPercentage" name="steamPumpPercentage" type="number" step="0.1"
                    value={String(formData.steamPumpPercentage * (pressureAvailable.value ? 0.1 : 1))}
                    placeholder={pressureAvailable.value ? '0.0' : '0.0'}
                    onBlur={e => setFormData({ ...formData, steamPumpPercentage: (parseFloat(e.target.value) * (pressureAvailable.value ? 10 : 1)).toFixed(0) })}
                    onChange={onChange('steamPumpPercentage')}
                  />
                  <span class="text-sm text-[--text-muted] shrink-0">{pressureAvailable.value ? 'ml/s' : '%'}</span>
                </div>
              </div>
              {pressureAvailable.value && (
                <div>
                  <FieldLabel htmlFor="steamPumpCutoff">Pump Assist Cutoff</FieldLabel>
                  <FieldHint>At how many bars should the pump assist stop.</FieldHint>
                  <div class="flex items-center gap-2">
                    <DarkInput id="steamPumpCutoff" name="steamPumpCutoff" type="number" step="any" value={formData.steamPumpCutoff} placeholder="0.0" onChange={onChange('steamPumpCutoff')} />
                    <span class="text-sm text-[--text-muted] shrink-0">bar</span>
                  </div>
                </div>
              )}
              <div>
                <FieldLabel htmlFor="altRelayFunction">Alt Relay / SSR2 Function</FieldLabel>
                <DarkSelect id="altRelayFunction" name="altRelayFunction" value={formData.altRelayFunction ?? 1} onChange={onChange('altRelayFunction')}>
                  <option value={0}>None</option>
                  <option value={1}>Grind</option>
                  <option value={2} disabled>Steam Boiler (Coming Soon)</option>
                </DarkSelect>
              </div>
            </div>
          </div>

          {/* Display Settings */}
          <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faPalette} title="Display Settings" />
            <div class="space-y-4">
              <div>
                <FieldLabel htmlFor="mainBrightness">Main Brightness (1-16)</FieldLabel>
                <DarkInput id="mainBrightness" name="mainBrightness" type="number" min="1" max="16" value={formData.mainBrightness} placeholder="16" onChange={onChange('mainBrightness')} />
              </div>
              <div class="pt-3" style="border-top: 1px solid var(--border);">
                <FieldLabel>Standby Display</FieldLabel>
                <div class="mb-3">
                  <DarkCheckbox id="standbyDisplayEnabled" name="standbyDisplayEnabled" checked={formData.standbyDisplayEnabled} onChange={onChange('standbyDisplayEnabled')} label="Enable standby display" />
                </div>
                <div class="space-y-3">
                  <div>
                    <FieldLabel htmlFor="standbyBrightness">Standby Brightness (0-16)</FieldLabel>
                    <DarkInput id="standbyBrightness" name="standbyBrightness" type="number" min="0" max="16" value={formData.standbyBrightness} placeholder="8" disabled={!formData.standbyDisplayEnabled} onChange={onChange('standbyBrightness')} />
                  </div>
                  <div>
                    <FieldLabel htmlFor="standbyBrightnessTimeout">Standby Brightness Timeout</FieldLabel>
                    <div class="flex items-center gap-2">
                      <DarkInput id="standbyBrightnessTimeout" name="standbyBrightnessTimeout" type="number" min="1" value={formData.standbyBrightnessTimeout} placeholder="60" onChange={onChange('standbyBrightnessTimeout')} />
                      <span class="text-sm text-[--text-muted] shrink-0">s</span>
                    </div>
                  </div>
                </div>
              </div>
              <div class="pt-3" style="border-top: 1px solid var(--border);">
                <FieldLabel htmlFor="themeMode">Theme</FieldLabel>
                <DarkSelect id="themeMode" name="themeMode" value={formData.themeMode} onChange={onChange('themeMode')}>
                  <option value={0}>Dark Theme</option>
                  <option value={1}>Light Theme</option>
                </DarkSelect>
              </div>
            </div>
          </div>

          {/* Sunrise Settings */}
          {ledControl.value && (
            <div class="rounded-xl p-5" style="background: var(--bg-elevated); border: 1px solid var(--border);">
              <SectionHeader icon={faSun} title="Sunrise Settings" />
              <FieldHint class="mb-4">Set the colors for the LEDs when in idle mode with no warnings.</FieldHint>
              <div class="grid grid-cols-2 gap-3 mb-4">
                {['sunriseR', 'sunriseG', 'sunriseB', 'sunriseW'].map(id => (
                  <div key={id}>
                    <FieldLabel htmlFor={id}>{id.replace('sunrise', '')} (0-255)</FieldLabel>
                    <DarkInput id={id} name={id} type="number" min="0" max="255" value={formData[id]} placeholder="16" onChange={onChange(id)} />
                  </div>
                ))}
              </div>
              <div class="space-y-3">
                <div>
                  <FieldLabel htmlFor="sunriseExtBrightness">External LED (0-255)</FieldLabel>
                  <DarkInput id="sunriseExtBrightness" name="sunriseExtBrightness" type="number" min="0" max="255" value={formData.sunriseExtBrightness} placeholder="16" onChange={onChange('sunriseExtBrightness')} />
                </div>
                <div>
                  <FieldLabel htmlFor="emptyTankDistance">Distance from sensor to bottom of tank</FieldLabel>
                  <div class="flex items-center gap-2">
                    <DarkInput id="emptyTankDistance" name="emptyTankDistance" type="number" value={formData.emptyTankDistance} placeholder="16" onChange={onChange('emptyTankDistance')} />
                    <span class="text-sm text-[--text-muted] shrink-0">mm</span>
                  </div>
                </div>
                <div>
                  <FieldLabel htmlFor="fullTankDistance">Distance from sensor to fill line</FieldLabel>
                  <div class="flex items-center gap-2">
                    <DarkInput id="fullTankDistance" name="fullTankDistance" type="number" value={formData.fullTankDistance} placeholder="16" onChange={onChange('fullTankDistance')} />
                    <span class="text-sm text-[--text-muted] shrink-0">mm</span>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* Google Drive Backup */}
          <GoogleDriveBackupCard apiService={apiService} onRestoreComplete={() => setGen(prev => prev + 1)} />

          {/* Plugins */}
          <div class="rounded-xl p-5 md:col-span-2" style="background: var(--bg-elevated); border: 1px solid var(--border);">
            <SectionHeader icon={faPlug} title="Plugins & Integration" />
            <PluginCard
              formData={formData}
              onChange={onChange}
              autowakeupSchedules={autowakeupSchedules}
              addAutoWakeupSchedule={addAutoWakeupSchedule}
              removeAutoWakeupSchedule={removeAutoWakeupSchedule}
              updateAutoWakeupTime={updateAutoWakeupTime}
              updateAutoWakeupDay={updateAutoWakeupDay}
            />
          </div>
        </div>

        {/* Save actions */}
        <div class="mt-6 rounded-xl p-4 flex flex-col sm:flex-row items-start sm:items-center gap-4" style="background: rgba(234,179,8,0.05); border: 1px solid rgba(234,179,8,0.2);">
          <div class="flex items-center gap-2 text-sm text-[--warning]">
            <svg viewBox="0 0 20 20" class="size-4 shrink-0" fill="currentColor">
              <path d="M8.485 2.495c.673-1.167 2.357-1.167 3.03 0l6.28 11.445c1.167 2.13.29 3.72-1.516 3.72H3.72c-1.807 0-2.683-1.59-1.516-3.72l6.28-11.445zm.538.465l6.28 11.445c.155.283.468.465.81.465h1.518c.89 0 1.337-1.12.702-1.73l-6.28-11.445a.97.97 0 00-.81-.465H9.823a.97.97 0 00-.81.465z" />
            </svg>
            <span>Some options like Wi-Fi, NTP, and managing plugins require a restart.</span>
          </div>
          <div class="flex items-center gap-2 ml-auto flex-wrap">
            <a href="/" class="inline-flex items-center justify-center px-5 py-2.5 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all">
              Back
            </a>
            <button
              type="submit"
              disabled={submitting}
              class="inline-flex items-center gap-2 px-5 py-2.5 rounded-lg text-sm font-semibold transition-all"
              style="background: var(--accent); color: var(--bg-base);"
            >
              {submitting && <div class="size-3.5 rounded-full border-2 border-t-transparent animate-spin" style="border-color: var(--bg-base);" />}
              Save
            </button>
            <button
              type="submit"
              name="restart"
              disabled={submitting}
              onClick={e => onSubmit(e, true)}
              class="inline-flex items-center gap-2 px-5 py-2.5 rounded-lg text-sm font-semibold transition-all text-[--text-primary] border border-[--border] hover:border-[--border-active]"
            >
              Save and Restart
            </button>
          </div>
        </div>
      </form>
    </div>
  );
}