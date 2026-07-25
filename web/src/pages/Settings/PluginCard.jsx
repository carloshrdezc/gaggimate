import { faTrashCan, faPlus } from '@fortawesome/free-solid-svg-icons';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { useEffect, useRef, useState } from 'preact/hooks';
import homekitImage from '../../assets/homekit.png';
import { CollapsibleHeader } from '../../components/CollapsibleHeader.jsx';

// A single collapsible plugin sub-card (PRO-572). Local open state, independent
// from Settings' top-level openMap and from the enable toggle:
//  - if the plugin is already enabled at mount (fetched enabled from
//    /api/settings), the body starts expanded so the settings are visible on
//    the first click into the Plugins card (PRO-573),
//  - toggling the plugin ON auto-expands the body (matches the prior behaviour
//    of revealing the body on enable),
//  - toggling OFF collapses AND resets open=false, so the next ON starts
//    expanded again,
//  - while enabled, the chevron manually collapses/expands the body without
//    touching the enable toggle.
// The body only renders while both enabled and open (the enable flag remains
// the wire-format source of truth; open is UI-only).
function PluginSubCard({ title, enabled, onToggle, children }) {
  // Start expanded if the plugin is already enabled at mount time (PRO-573):
  // a plugin fetched as enabled from /api/settings should reveal its body on
  // the first click into the Plugins card, not require a second click.
  const [open, setOpen] = useState(enabled);
  const prevEnabled = useRef(enabled);

  useEffect(() => {
    if (enabled && !prevEnabled.current) {
      setOpen(true); // auto-expand on OFF -> ON
    } else if (!enabled && prevEnabled.current) {
      setOpen(false); // reset on ON -> OFF
    }
    prevEnabled.current = enabled;
  }, [enabled]);

  return (
    <div className='nd-card p-4'>
      <div className='flex items-center justify-between gap-3'>
        <span className='font-nd-mono text-[16px] text-[var(--text-primary,#e8e8e8)]'>{title}</span>
        <div className='flex items-center gap-2'>
          {enabled && (
            <CollapsibleHeader
              open={open}
              onToggle={() => setOpen(o => !o)}
              asChevron
              ariaLabel={`${open ? 'Collapse' : 'Expand'} ${title}`}
            />
          )}
          <button
            type='button'
            className={`nd-toggle ${enabled ? 'nd-toggle--active' : ''}`}
            onClick={onToggle}
            role='switch'
            aria-checked={!!enabled}
          >
            <span className='nd-toggle-thumb' />
          </button>
        </div>
      </div>
      {/* When enabled, keep the body mounted even while collapsed and hide it
          with the `hidden` attribute instead of unmounting it (PRO-572).
          Settings' onSubmit builds the save payload from `new FormData(form)`,
          which only captures inputs currently in the DOM, and these plugin
          sub-fields (startupFillTime, smartGrindIp, haIP, …) have no explicit
          form.set()/delete() override in onSubmit — so unmounting a collapsed
          (but still enabled) sub-card silently dropped any field the user
          edited before collapsing it. `hidden` keeps every input mounted so
          FormData still captures it regardless of collapse state. When the
          plugin is disabled the body stays unmounted on purpose: the enable
          toggle is handled explicitly in onSubmit and a disabled plugin's
          sub-fields should be absent from the payload. */}
      {enabled && <div hidden={!open}>{children}</div>}
    </div>
  );
}

export function PluginCard({
  formData,
  onChange,
  autowakeupSchedules,
  addAutoWakeupSchedule,
  removeAutoWakeupSchedule,
  updateAutoWakeupTime,
  updateAutoWakeupDay,
}) {
  return (
    <div className='flex flex-col gap-5'>
      {/* Automatic Wakeup Schedule */}
      <PluginSubCard
        title='Automatic Wakeup Schedule'
        enabled={formData.autowakeupEnabled}
        onToggle={onChange('autowakeupEnabled')}
      >
        <div className='mt-5 border-t border-[var(--home-border,#222)] pt-4'>
          <div className='font-nd-mono mb-4 text-[13px] text-[var(--text-disabled,#666)]'>
            Automatically switch to brew mode at specified time(s) of day.
          </div>
          <div className='flex flex-col gap-4'>
            {autowakeupSchedules?.map((schedule, scheduleIndex) => (
              <div key={scheduleIndex} className='flex flex-wrap items-center gap-3'>
                {/* Time input */}
                <input
                  type='time'
                  className='nd-input'
                  style={{ width: 'auto', minWidth: '0' }}
                  value={schedule.time}
                  onChange={e => updateAutoWakeupTime(scheduleIndex, e.target.value)}
                  disabled={!formData.autowakeupEnabled}
                />

                {/* Days toggle buttons */}
                <div className='flex gap-1' role='group' aria-label='Days of week selection'>
                  {['M', 'T', 'W', 'T', 'F', 'S', 'S'].map((dayLabel, dayIndex) => (
                    <button
                      key={dayIndex}
                      type='button'
                      className={`nd-day-btn ${schedule.days[dayIndex] ? 'nd-day-btn--active' : ''}`}
                      onClick={() =>
                        updateAutoWakeupDay(scheduleIndex, dayIndex, !schedule.days[dayIndex])
                      }
                      disabled={!formData.autowakeupEnabled}
                      aria-pressed={schedule.days[dayIndex]}
                      aria-label={
                        [
                          'Monday',
                          'Tuesday',
                          'Wednesday',
                          'Thursday',
                          'Friday',
                          'Saturday',
                          'Sunday',
                        ][dayIndex]
                      }
                      title={
                        [
                          'Monday',
                          'Tuesday',
                          'Wednesday',
                          'Thursday',
                          'Friday',
                          'Saturday',
                          'Sunday',
                        ][dayIndex]
                      }
                    >
                      {dayLabel}
                    </button>
                  ))}
                </div>

                {/* Delete button */}
                {autowakeupSchedules.length > 1 ? (
                  <button
                    type='button'
                    onClick={() => removeAutoWakeupSchedule(scheduleIndex)}
                    className='nd-action-btn'
                    style={{ width: '36px', height: '36px' }}
                    disabled={!formData.autowakeupEnabled}
                    title='Delete this schedule'
                  >
                    <FontAwesomeIcon icon={faTrashCan} />
                  </button>
                ) : (
                  <div
                    className='nd-action-btn'
                    style={{ width: '36px', height: '36px', opacity: 0.3, cursor: 'not-allowed' }}
                    title='Cannot delete the last schedule'
                  >
                    <FontAwesomeIcon icon={faTrashCan} />
                  </div>
                )}
              </div>
            ))}
            <button
              type='button'
              onClick={addAutoWakeupSchedule}
              className='nd-action-btn nd-action-btn--primary nd-action-btn--text'
              style={{ width: 'fit-content' }}
              disabled={!formData.autowakeupEnabled}
            >
              <FontAwesomeIcon icon={faPlus} />
              Add Schedule
            </button>
          </div>
        </div>
      </PluginSubCard>

      {/* HomeKit */}
      <PluginSubCard title='HomeKit' enabled={formData.homekit} onToggle={onChange('homekit')}>
        <div className='mt-5 flex flex-col items-center justify-center gap-4 border-t border-[var(--home-border,#222)] pt-4'>
          <img src={homekitImage} alt='HomeKit Setup Code' />
          <p className='font-nd-mono text-center text-[13px] text-[var(--text-disabled,#666)]'>
            Open the Home app on your iOS device, select Add Accessory, and enter the setup code
            shown above.
          </p>
        </div>
      </PluginSubCard>

      {/* Boiler Refill Plugin */}
      <PluginSubCard
        title='Boiler Refill Plugin'
        enabled={formData.boilerFillActive}
        onToggle={onChange('boilerFillActive')}
      >
        <div className='mt-5 grid grid-cols-2 gap-4 border-t border-[var(--home-border,#222)] pt-4'>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='startupFillTime'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              On startup (s)
            </label>
            <input
              id='startupFillTime'
              name='startupFillTime'
              type='number'
              inputMode='decimal'
              className='nd-input'
              placeholder='0'
              value={formData.startupFillTime}
              onChange={onChange('startupFillTime')}
            />
          </div>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='steamFillTime'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              On steam deactivate (s)
            </label>
            <input
              id='steamFillTime'
              name='steamFillTime'
              type='number'
              inputMode='decimal'
              className='nd-input'
              placeholder='0'
              value={formData.steamFillTime}
              onChange={onChange('steamFillTime')}
            />
          </div>
        </div>
      </PluginSubCard>

      {/* Smart Grind Plugin */}
      <PluginSubCard
        title='Smart Grind Plugin'
        enabled={formData.smartGrindActive}
        onToggle={onChange('smartGrindActive')}
      >
        <div className='mt-5 flex flex-col gap-4 border-t border-[var(--home-border,#222)] pt-4'>
          <div className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)]'>
            This feature controls a Tasmota Plug to turn off your grinder after the target has been
            reached.
          </div>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='smartGrindIp'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              Tasmota IP
            </label>
            <input
              id='smartGrindIp'
              name='smartGrindIp'
              type='text'
              className='nd-input'
              placeholder='0'
              value={formData.smartGrindIp}
              onChange={onChange('smartGrindIp')}
            />
          </div>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='smartGrindMode'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              Mode
            </label>
            <select
              id='smartGrindMode'
              name='smartGrindMode'
              className='nd-input'
              onChange={onChange('smartGrindMode')}
            >
              <option value='0' selected={formData.smartGrindMode?.toString() === '0'}>
                Turn off at target
              </option>
              <option value='1' selected={formData.smartGrindMode?.toString() === '1'}>
                Toggle off and on at target
              </option>
              <option value='2' selected={formData.smartGrindMode?.toString() === '2'}>
                Turn on at start, off at target
              </option>
            </select>
          </div>
        </div>
      </PluginSubCard>

      {/* Home Assistant over MQTT (Deprecated) — header + toggle always visible; deprecated body shown only when enabled (PRO-323) */}
      <PluginSubCard
        title='Home Assistant over MQTT (Deprecated)'
        enabled={formData.homeAssistant}
        onToggle={onChange('homeAssistant')}
      >
        <div className='mt-5 flex flex-col gap-4 border-t border-[var(--home-border,#222)] pt-4'>
          <div className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)]'>
            This feature allows connection to a Home Assistant or MQTT installation and push the
            current state. This feature is deprecated for usage with Home Assistant. Please see the{' '}
            <a
              href='https://github.com/gaggimate/ha-integration'
              target='_blank'
              rel='noreferrer'
              className='text-[var(--color-primary,#d71921)]'
            >
              Home Assistant Integration
            </a>{' '}
            for a more up-to-date solution.
          </div>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='haIP'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              MQTT IP
            </label>
            <input
              id='haIP'
              name='haIP'
              type='text'
              className='nd-input'
              placeholder='0'
              value={formData.haIP}
              onChange={onChange('haIP')}
            />
          </div>

          <div className='flex flex-col gap-2'>
            <label
              htmlFor='haPort'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              MQTT Port
            </label>
            <input
              id='haPort'
              name='haPort'
              type='number'
              inputMode='numeric'
              className='nd-input'
              placeholder='0'
              value={formData.haPort}
              onChange={onChange('haPort')}
            />
          </div>

          <div className='flex flex-col gap-2'>
            <label
              htmlFor='haUser'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              MQTT User
            </label>
            <input
              id='haUser'
              name='haUser'
              type='text'
              className='nd-input'
              placeholder='user'
              value={formData.haUser}
              onChange={onChange('haUser')}
            />
          </div>

          <div className='flex flex-col gap-2'>
            <label
              htmlFor='haPassword'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              MQTT Password
            </label>
            <input
              id='haPassword'
              name='haPassword'
              type='password'
              className='nd-input'
              placeholder='password'
              value={formData.haPassword}
              onChange={onChange('haPassword')}
            />
          </div>
          <div className='flex flex-col gap-2'>
            <label
              htmlFor='haTopic'
              className='font-nd-mono text-[14px] tracking-[0.08em] text-[var(--text-secondary,#999)] uppercase'
            >
              Home Assistant Discovery Topic
            </label>
            <input
              id='haTopic'
              name='haTopic'
              type='text'
              className='nd-input'
              value={formData.haTopic}
              onChange={onChange('haTopic')}
            />
          </div>
        </div>
      </PluginSubCard>
    </div>
  );
}
