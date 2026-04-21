import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faHome } from '@fortawesome/free-solid-svg-icons/faHome';
import { faWater } from '@fortawesome/free-solid-svg-icons/faWater';
import { faGear } from '@fortawesome/free-solid-svg-icons/faGear';
import { faHome as faHomeAssistant } from '@fortawesome/free-solid-svg-icons/faHome';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import homekitImage from '../../assets/homekit.png';

const PluginSection = ({ icon, title, children }) => (
  <div class="rounded-xl p-4" style="background: var(--bg-base); border: 1px solid var(--border);">
    <div class="flex items-center justify-between mb-4">
      <div class="flex items-center gap-2">
        <FontAwesomeIcon icon={icon} class="text-sm text-[--accent]" />
        <span class="text-sm font-semibold text-[--text-primary]">{title}</span>
      </div>
    </div>
    {children}
  </div>
);

const Toggle = ({ id, name, checked, onChange, label }) => (
  <label class="flex items-center gap-3 cursor-pointer group">
    <div
      class="relative size-9 rounded-lg flex items-center justify-center transition-all"
      style="background: var(--bg-elevated); border: 1px solid var(--border);"
      onClick={() => onChange({ currentTarget: { name, value: !checked } })}
    >
      <div class="size-4 rounded transition-all" style={`background: ${checked ? 'var(--accent)' : 'transparent'};`}>
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

const FieldLabel = ({ children }) => (
  <label class="block text-xs font-medium text-[--text-secondary] mb-1.5">{children}</label>
);

const DarkInput = ({ id, name, type = 'text', value, placeholder, onChange, disabled }) => (
  <input
    id={id} name={name} type={type} value={value} placeholder={placeholder} disabled={disabled} onChange={onChange}
    class="w-full px-3 py-2.5 rounded-lg text-sm text-[--text-primary] placeholder:text-[--text-muted] transition-all"
    style={`background: var(--bg-elevated); border: 1px solid var(--border); outline: none; ${disabled ? 'opacity: 0.5; cursor: not-allowed;' : ''}`}
    onFocus={e => !disabled && (e.target.style.borderColor = 'var(--accent)')}
    onBlur={e => !disabled && (e.target.style.borderColor = 'var(--border)')}
  />
);

const DarkSelect = ({ id, name, value, onChange, children, disabled }) => (
  <select
    id={id} name={name} value={value} disabled={disabled} onChange={onChange}
    class="w-full px-3 py-2 rounded-lg text-sm text-[--text-primary] cursor-pointer transition-all"
    style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
    onFocus={e => e.target.style.borderColor = 'var(--accent)'}
    onBlur={e => e.target.style.borderColor = 'var(--border)'}
  >
    {children}
  </select>
);

export function PluginCard({
  formData, onChange,
  autowakeupSchedules, addAutoWakeupSchedule, removeAutoWakeupSchedule,
  updateAutoWakeupTime, updateAutoWakeupDay,
}) {
  return (
    <div class="space-y-4">
      {/* Auto Wakeup Schedule */}
      <PluginSection icon={faClock} title="Automatic Wakeup Schedule">
        <div class="flex items-center justify-between mb-3">
          <Toggle
            id="autowakeupEnabled" name="autowakeupEnabled"
            checked={!!formData.autowakeupEnabled}
            onChange={onChange('autowakeupEnabled')}
            label="Enable Auto Wakeup"
          />
        </div>
        {formData.autowakeupEnabled && (
          <div class="pt-3 mt-3" style="border-top: 1px solid var(--border);">
            <p class="text-xs text-[--text-muted] mb-4">Automatically switch to brew mode at specified time(s).</p>
            <div class="space-y-3">
              {autowakeupSchedules?.map((schedule, scheduleIndex) => (
                <div key={scheduleIndex} class="flex items-center gap-2 flex-wrap">
                  <input
                    type="time"
                    value={schedule.time}
                    onChange={e => updateAutoWakeupTime(scheduleIndex, e.target.value)}
                    disabled={!formData.autowakeupEnabled}
                    class="px-3 py-2 rounded-lg text-sm text-[--text-primary]"
                    style="background: var(--bg-elevated); border: 1px solid var(--border); outline: none;"
                    onFocus={e => e.target.style.borderColor = 'var(--accent)'}
                    onBlur={e => e.target.style.borderColor = 'var(--border)'}
                  />
                  <div class="flex gap-1" role="group" aria-label="Days of week">
                    {['M', 'T', 'W', 'T', 'F', 'S', 'S'].map((dayLabel, dayIndex) => (
                      <button
                        key={dayIndex}
                        type="button"
                        onClick={() => updateAutoWakeupDay(scheduleIndex, dayIndex, !schedule.days[dayIndex])}
                        disabled={!formData.autowakeupEnabled}
                        aria-pressed={schedule.days[dayIndex]}
                        class="size-7 rounded-lg text-xs font-medium transition-all"
                        style={schedule.days[dayIndex]
                          ? 'background: var(--accent); color: var(--bg-base);'
                          : 'background: var(--bg-elevated); color: var(--text-secondary); border: 1px solid var(--border);'}
                      >
                        {dayLabel}
                      </button>
                    ))}
                  </div>
                  {autowakeupSchedules.length > 1 ? (
                    <button
                      type="button"
                      onClick={() => removeAutoWakeupSchedule(scheduleIndex)}
                      disabled={!formData.autowakeupEnabled}
                      class="p-1.5 rounded-lg text-[--error] hover:bg-[--error]/10 transition-all"
                      title="Delete schedule"
                    >
                      <FontAwesomeIcon icon={faTrashCan} class="text-xs" />
                    </button>
                  ) : (
                    <div class="p-1.5 opacity-30" title="Cannot delete last schedule">
                      <FontAwesomeIcon icon={faTrashCan} class="text-xs text-[--text-muted]" />
                    </div>
                  )}
                </div>
              ))}
              <button
                type="button"
                onClick={addAutoWakeupSchedule}
                disabled={!formData.autowakeupEnabled}
                class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium text-[--accent] border border-[--accent]/30 hover:bg-[--accent]/10 transition-all"
              >
                <FontAwesomeIcon icon={faPlus} />
                Add Schedule
              </button>
            </div>
          </div>
        )}
      </PluginSection>

      {/* HomeKit */}
      <PluginSection icon={faHome} title="HomeKit">
        <div class="flex items-center justify-between">
          <Toggle
            id="homekit" name="homekit"
            checked={!!formData.homekit}
            onChange={onChange('homekit')}
            label="Enable HomeKit"
          />
        </div>
        {formData.homekit && (
          <div class="mt-4 pt-4 flex flex-col items-center gap-4" style="border-top: 1px solid var(--border);">
            <img src={homekitImage} alt="HomeKit Setup Code" class="max-w-[160px] rounded-lg" />
            <p class="text-xs text-[--text-muted] text-center">
              Open the Home app on your iOS device, select Add Accessory, and enter the setup code shown above.
            </p>
          </div>
        )}
      </PluginSection>

      {/* Boiler Refill */}
      <PluginSection icon={faWater} title="Boiler Refill Plugin">
        <div class="flex items-center justify-between mb-4">
          <Toggle
            id="boilerFillActive" name="boilerFillActive"
            checked={!!formData.boilerFillActive}
            onChange={onChange('boilerFillActive')}
            label="Enable Boiler Refill"
          />
        </div>
        {formData.boilerFillActive && (
          <div class="pt-4 mt-4 grid grid-cols-2 gap-4" style="border-top: 1px solid var(--border);">
            <div>
              <FieldLabel htmlFor="startupFillTime">On startup (s)</FieldLabel>
              <DarkInput id="startupFillTime" name="startupFillTime" type="number" value={formData.startupFillTime} placeholder="0" onChange={onChange('startupFillTime')} />
            </div>
            <div>
              <FieldLabel htmlFor="steamFillTime">On steam deactivate (s)</FieldLabel>
              <DarkInput id="steamFillTime" name="steamFillTime" type="number" value={formData.steamFillTime} placeholder="0" onChange={onChange('steamFillTime')} />
            </div>
          </div>
        )}
      </PluginSection>

      {/* Smart Grind */}
      <PluginSection icon={faGear} title="Smart Grind Plugin">
        <div class="flex items-center justify-between mb-4">
          <Toggle
            id="smartGrindActive" name="smartGrindActive"
            checked={!!formData.smartGrindActive}
            onChange={onChange('smartGrindActive')}
            label="Enable Smart Grind"
          />
        </div>
        {formData.smartGrindActive && (
          <div class="pt-4 mt-4 space-y-4" style="border-top: 1px solid var(--border);">
            <p class="text-xs text-[--text-muted]">This feature controls a Tasmota Plug to turn off your grinder after the target has been reached.</p>
            <div>
              <FieldLabel htmlFor="smartGrindIp">Tasmota IP</FieldLabel>
              <DarkInput id="smartGrindIp" name="smartGrindIp" type="text" value={formData.smartGrindIp} placeholder="0" onChange={onChange('smartGrindIp')} />
            </div>
            <div>
              <FieldLabel htmlFor="smartGrindMode">Mode</FieldLabel>
              <DarkSelect id="smartGrindMode" name="smartGrindMode" value={formData.smartGrindMode ?? 0} onChange={onChange('smartGrindMode')}>
                <option value="0" selected={formData.smartGrindMode?.toString() === '0'}>Turn off at target</option>
                <option value="1" selected={formData.smartGrindMode?.toString() === '1'}>Toggle off and on at target</option>
                <option value="2" selected={formData.smartGrindMode?.toString() === '2'}>Turn on at start, off at target</option>
              </DarkSelect>
            </div>
          </div>
        )}
      </PluginSection>

      {/* Home Assistant (Deprecated) */}
      <PluginSection icon={faHomeAssistant} title="Home Assistant over MQTT (Deprecated)">
        <div class="flex items-center justify-between mb-4">
          <Toggle
            id="homeAssistant" name="homeAssistant"
            checked={!!formData.homeAssistant}
            onChange={onChange('homeAssistant')}
            label="Enable Home Assistant"
          />
        </div>
        {formData.homeAssistant && (
          <div class="pt-4 mt-4 space-y-4" style="border-top: 1px solid var(--border);">
            <p class="text-xs text-[--text-muted]">
              This feature allows connection to a Home Assistant or MQTT installation. This feature is deprecated. Please see the{' '}
              <a href="https://github.com/gaggimate/ha-integration" target="_blank" rel="noreferrer" class="text-[--accent] hover:underline">Home Assistant Integration</a>.
            </p>
            <div class="grid grid-cols-2 gap-4">
              <div>
                <FieldLabel htmlFor="haIP">MQTT IP</FieldLabel>
                <DarkInput id="haIP" name="haIP" type="text" value={formData.haIP} placeholder="0" onChange={onChange('haIP')} />
              </div>
              <div>
                <FieldLabel htmlFor="haPort">MQTT Port</FieldLabel>
                <DarkInput id="haPort" name="haPort" type="number" value={formData.haPort} placeholder="0" onChange={onChange('haPort')} />
              </div>
              <div>
                <FieldLabel htmlFor="haUser">MQTT User</FieldLabel>
                <DarkInput id="haUser" name="haUser" type="text" value={formData.haUser} placeholder="user" onChange={onChange('haUser')} />
              </div>
              <div>
                <FieldLabel htmlFor="haPassword">MQTT Password</FieldLabel>
                <DarkInput id="haPassword" name="haPassword" type="password" value={formData.haPassword} placeholder="password" onChange={onChange('haPassword')} />
              </div>
            </div>
            <div>
              <FieldLabel htmlFor="haTopic">Home Assistant Discovery Topic</FieldLabel>
              <DarkInput id="haTopic" name="haTopic" type="text" value={formData.haTopic} onChange={onChange('haTopic')} />
            </div>
          </div>
        )}
      </PluginSection>
    </div>
  );
}