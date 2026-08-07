// D · Merged design — multi-ring gauge + live extraction graph + recipe targets.
// Ring (top-left) | Recipe + targets (top-right) | Full-width graph (bottom).

import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { computed } from '@preact/signals';
import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { authenticatedFetch } from '../../services/localAuthFetch.js';
import { useProcessActions } from '../../hooks/useProcessActions.js';
import { useProfileData } from '../../hooks/useProfileData.js';
import { useAutoSteam } from '../../hooks/useAutoSteam.js';
import { useStandbyOnBrew } from '../../hooks/useStandbyOnBrew.js';
import { useActiveShotManualGrindRecorder } from '../../hooks/useActiveShotManualGrindRecorder.js';
import { useShotDoseRecorder } from '../../hooks/useShotDoseRecorder.js';
import { useGrindSettings } from '../../hooks/useGrindSettings.js';
import { listBeans, recordBeanSelection, parseQuantity } from '../../utils/beanManager.js';
import {
  listGrinders,
  recordGrinderSelection,
  recordManualGrindSetting,
  MANUAL_GRIND_SETTING_STORAGE_KEY,
} from '../../utils/grinderManager.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faGithub } from '@fortawesome/free-brands-svg-icons/faGithub';
import { faDiscord } from '@fortawesome/free-brands-svg-icons/faDiscord';
import PropTypes from 'prop-types';
import {
  MANUAL_FLOW_MAX,
  MANUAL_FLOW_MIN,
  MANUAL_PRESSURE_MAX,
  MANUAL_PRESSURE_MIN,
  MANUAL_TARGET_FLOW,
  MANUAL_TARGET_PRESSURE,
  MANUAL_TEMP_MAX,
  MANUAL_TEMP_MIN,
  MODE_MANUAL,
  MODE_STANDBY,
  MODE_STEAM,
  buildStandbyProfileCurve,
  clampManualFlow,
  clampManualPressure,
  clampManualTemperature,
  computeYieldEditable,
  getReadinessSummary,
  getYieldLockReason,
  getAvailableModeOptions,
  getBoilerHeatingState,
  getManualControlLabels,
  getProcessKindForMode,
  getPrimaryActionState,
  getTemperatureRingMetrics,
  computeStandbyOnBrewButtonState,
  shouldFireAutoSteamOnStop,
  shouldFireStandbyOnStop,
  shouldKeepManualDraftDirty,
  shouldSendManualUpdate,
} from './dashboardLogic.js';

const DOSE_KEY = 'gaggimate-dose-grams';
const DEFAULT_DOSE = 18.0;
const DEFAULT_YIELD = 36.0;

// Manual grinder-dial setting (PRO-431, made device-authoritative in PRO-603).
// localStorage key holds the physical grinder dial number as an offline seed
// only; the device now owns the value (broadcast as `mg` in evt:status, mapped
// to s.manualGrindSetting) and every commit fires req:manual-grind:set so the
// value stays in sync across browsers, matching DOSE. Default 0 = "not set yet".
const DEFAULT_MANUAL_GRIND = 0;

const PRESSURE_MAX = 12;
const FLOW_MAX = 6;
const RING_TOTAL_ARC = 300;
const RING_START_ANGLE = 210;
const YIELD_SEGMENTS = Array.from({ length: 40 }, (_, i) => i);
// PRO-426: standby profile mini-curve viewBox (SVG stretches to its container).
const STANDBY_CURVE_VB_W = 300;
const STANDBY_CURVE_VB_H = 90;
const MANUAL_TARGET_OPTIONS = [
  { id: MANUAL_TARGET_PRESSURE, label: 'PRESSURE' },
  { id: MANUAL_TARGET_FLOW, label: 'FLOW' },
];

const status = computed(() => machine.value.status);

// ── SVG ring path helpers ───────────────────────────────────────────────────

function arcPath(cx, cy, r, frac) {
  if (frac <= 0) return '';
  const clamped = Math.min(1, frac);
  const a0 = (RING_START_ANGLE * Math.PI) / 180;
  const a1 = ((RING_START_ANGLE + RING_TOTAL_ARC * clamped) * Math.PI) / 180;
  const large = RING_TOTAL_ARC * clamped > 180 ? 1 : 0;
  const x0 = (cx + r * Math.cos(a0)).toFixed(2);
  const y0 = (cy + r * Math.sin(a0)).toFixed(2);
  const x1 = (cx + r * Math.cos(a1)).toFixed(2);
  const y1 = (cy + r * Math.sin(a1)).toFixed(2);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`;
}

function trackPath(cx, cy, r) {
  return arcPath(cx, cy, r, 1);
}

function tickPath(cx, cy, r, frac) {
  const a = ((RING_START_ANGLE + RING_TOTAL_ARC * Math.min(1, Math.max(0, frac))) * Math.PI) / 180;
  const x0 = (cx + (r - 6) * Math.cos(a)).toFixed(2);
  const y0 = (cy + (r - 6) * Math.sin(a)).toFixed(2);
  const x1 = (cx + (r + 6) * Math.cos(a)).toFixed(2);
  const y1 = (cy + (r + 6) * Math.sin(a)).toFixed(2);
  return `M ${x0} ${y0} L ${x1} ${y1}`;
}

// ── Formatting helpers ──────────────────────────────────────────────────────

function fmt(n, dec = 1) {
  if (n == null || !Number.isFinite(n)) return '—';
  return n.toFixed(dec);
}

function fmtTimer(totalSecs) {
  const s = Math.max(0, Math.round(totalSecs));
  const m = Math.floor(s / 60);
  return `${String(m).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
}

function normalizeManualTargetType(value) {
  return MANUAL_TARGET_OPTIONS.some(option => option.id === value) ? value : MANUAL_TARGET_PRESSURE;
}

function manualDraftFromStatus(s) {
  return {
    targetType: normalizeManualTargetType(s.manualTargetType),
    pressure: clampManualPressure(s.manualPressure ?? s.targetPressure ?? 9),
    flow: clampManualFlow(s.manualFlow ?? s.targetFlow ?? 2),
    temperature: clampManualTemperature(s.manualTemperature ?? s.targetTemperature ?? 93),
  };
}

function getPrimaryActionButtonStyle({ active, finished, accent }) {
  return {
    background: active
      ? accent
      : finished
        ? 'var(--dm-good)'
        : `color-mix(in srgb, ${accent} 14%, transparent)`,
    color: active || finished ? '#fff' : accent,
    border: active || finished ? 'none' : `1px solid color-mix(in srgb, ${accent} 40%, transparent)`,
    fontFamily: 'var(--dm-font-display)',
    fontSize: 13,
    letterSpacing: '0.18em',
    padding: '10px 8px',
    borderRadius: 8,
    cursor: 'pointer',
    fontWeight: 700,
    boxShadow: active ? `0 6px 18px color-mix(in srgb, ${accent} 22%, transparent)` : 'none',
    transition: 'background 0.15s, box-shadow 0.15s',
  };
}

// ── Sub-components ──────────────────────────────────────────────────────────

export function ModeRail({ active, modes, onSelect }) {
  return (
    <div style={{ display: 'flex', gap: 5 }}>
      {modes.map(({ id, name }) => (
        <button
          key={name}
          type='button'
          onClick={() => onSelect(id)}
          aria-pressed={id === active}
          style={{
            padding: '5px 9px',
            borderRadius: 6,
            cursor: 'pointer',
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 12,
            letterSpacing: '0.22em',
            textTransform: 'uppercase',
            border: `1px solid ${id === active ? 'color-mix(in srgb, var(--dm-accent) 60%, transparent)' : 'var(--dm-line)'}`,
            background: id === active ? 'color-mix(in srgb, var(--dm-accent) 12%, transparent)' : 'transparent',
            color: id === active ? 'var(--dm-accent)' : 'var(--dm-fg-dim)',
          }}
        >
          {name}
        </button>
      ))}
    </div>
  );
}

ModeRail.propTypes = { active: PropTypes.number, modes: PropTypes.arrayOf(PropTypes.object), onSelect: PropTypes.func };

function StatusPill({ ledClass, label, value }) {
  return (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 5 }}>
      <span className={`dm-led ${ledClass}`} />
      <span
        style={{
          fontFamily: 'var(--dm-font-mono)',
          fontSize: 9,
          letterSpacing: '0.18em',
          color: 'var(--dm-fg-dim)',
        }}
      >
        {label}
      </span>
      <span
        style={{
          fontFamily: 'var(--dm-font-mono)',
          fontSize: 10,
          color: 'var(--dm-fg)',
          letterSpacing: '0.04em',
        }}
      >
        {value}
      </span>
    </span>
  );
}

StatusPill.propTypes = { ledClass: PropTypes.string, label: PropTypes.string, value: PropTypes.string };

function PhaseChip({ label, isActive, isDone }) {
  return (
    <span
      className={isActive ? 'dm-pulse' : ''}
      style={{
        fontFamily: 'var(--dm-font-mono)',
        fontSize: 9,
        letterSpacing: '0.18em',
        padding: '4px 8px',
        borderRadius: 4,
        border: `1px solid ${isActive ? 'color-mix(in srgb, var(--dm-accent) 50%, transparent)' : 'var(--dm-line)'}`,
        background: isActive ? 'color-mix(in srgb, var(--dm-accent) 10%, transparent)' : 'transparent',
        color: isActive ? 'var(--dm-accent)' : isDone ? 'var(--dm-fg-dim)' : 'var(--dm-fg-faint)',
        whiteSpace: 'nowrap',
      }}
    >
      {label}
    </span>
  );
}

PhaseChip.propTypes = { label: PropTypes.string, isActive: PropTypes.bool, isDone: PropTypes.bool };

function TargetBar({ color, label, cur, tgt, unit, frac, tgtFrac }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <span
          style={{
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 9,
            letterSpacing: '0.18em',
            color,
          }}
        >
          {label}
        </span>
        <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10 }}>
          <span style={{ color: 'var(--dm-fg)' }}>{cur}</span>
          <span style={{ color: 'var(--dm-fg-faint)' }}> / {tgt} {unit}</span>
        </span>
      </div>
      <div
        style={{
          position: 'relative',
          height: 5,
          background: 'var(--dm-bg-3)',
          borderRadius: 3,
          overflow: 'visible',
        }}
      >
        <div
          style={{
            position: 'absolute',
            inset: 0,
            width: `${Math.min(100, Math.max(0, frac * 100))}%`,
            background: color,
            borderRadius: 3,
          }}
        />
        {tgtFrac != null && (
          <div
            style={{
              position: 'absolute',
              top: -3,
              bottom: -3,
              left: `${Math.min(100, Math.max(0, tgtFrac * 100))}%`,
              width: 2,
              background: 'var(--dm-fg-dim)',
              borderRadius: 1,
            }}
          />
        )}
      </div>
    </div>
  );
}

TargetBar.propTypes = {
  color: PropTypes.string,
  label: PropTypes.string,
  cur: PropTypes.string,
  tgt: PropTypes.string,
  unit: PropTypes.string,
  frac: PropTypes.number,
  tgtFrac: PropTypes.number,
};

// PRO-426: Standby shot-card block. Replaces the live target bars + yield bar
// when the machine is idle (mode 0, non-manual). Surfaces a dark-themed
// hand-rolled mini-curve of the selected profile — no Chart.js. Grinder + grind
// setting are NOT duplicated here: the recipe row already renders them as
// editable dropdowns in all modes (incl. standby). Every field degrades to a
// themed empty-state string (never NaN / "undefined" / broken UI).
export function StandbyBlock({ profileName, curve, profileError, onRetryProfile }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12, minWidth: 0 }}>
      {/* Selected-profile mini-curve. */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 5, minWidth: 0 }}>
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'baseline',
          }}
        >
          <span
            style={{
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              letterSpacing: '0.18em',
              color: 'var(--dm-fg-dim)',
            }}
          >
            PROFILE
          </span>
          {curve && (
            <span style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
              <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 8, letterSpacing: '0.08em', color: 'var(--dm-good)' }}>
                ▬ PRESSURE
              </span>
              <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 8, letterSpacing: '0.08em', color: 'var(--dm-warn)' }}>
                ▬ FLOW
              </span>
              {curve.temperature && (
                <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 8, letterSpacing: '0.08em', color: 'var(--dm-accent)' }}>
                  ▬ TEMP
                </span>
              )}
            </span>
          )}
        </div>
        <div
          style={{
            border: '1px solid var(--dm-line)',
            borderRadius: 8,
            background: 'var(--dm-bg-0)',
            overflow: 'hidden',
            height: 92,
          }}
        >
          {curve ? (
            <svg
              viewBox={`0 0 ${STANDBY_CURVE_VB_W} ${STANDBY_CURVE_VB_H}`}
              preserveAspectRatio='none'
              style={{ width: '100%', height: '100%', display: 'block' }}
            >
              {/* Faint horizontal grid lines (quarters) */}
              {[0.25, 0.5, 0.75].map(f => (
                <line
                  key={f}
                  x1='0'
                  x2={STANDBY_CURVE_VB_W}
                  y1={(STANDBY_CURVE_VB_H * f).toFixed(1)}
                  y2={(STANDBY_CURVE_VB_H * f).toFixed(1)}
                  stroke='rgba(255,255,255,0.06)'
                  strokeDasharray='2 4'
                />
              ))}
              {/* Temperature (accent/red) under flow + pressure; pressure stays on top */}
              {curve.temperature && (
                <polyline
                  points={curve.temperature}
                  fill='none'
                  stroke='var(--dm-accent)'
                  strokeWidth='1.5'
                  strokeLinejoin='round'
                  strokeLinecap='round'
                  vectorEffect='non-scaling-stroke'
                />
              )}
              {/* Flow (amber) then pressure (green) on top */}
              <polyline
                points={curve.flow}
                fill='none'
                stroke='var(--dm-warn)'
                strokeWidth='1.5'
                strokeLinejoin='round'
                strokeLinecap='round'
                vectorEffect='non-scaling-stroke'
              />
              <polyline
                points={curve.pressure}
                fill='none'
                stroke='var(--dm-good)'
                strokeWidth='2'
                strokeLinejoin='round'
                strokeLinecap='round'
                vectorEffect='non-scaling-stroke'
              />
            </svg>
          ) : (
            <div
              style={{
                width: '100%',
                height: '100%',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                fontFamily: 'var(--dm-font-mono)',
                fontSize: 10,
                letterSpacing: '0.2em',
                color: 'var(--dm-fg-faint)',
                border: '1px dashed rgba(232,232,232,0.15)',
                borderRadius: 8,
              }}
            >
              {profileError ? (
                <span role='status' aria-live='polite' style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  PROFILE CURVE FAILED TO LOAD
                  <button type='button' onClick={onRetryProfile} style={retryButtonStyle}>RETRY</button>
                </span>
              ) : profileName ? 'PROFILE TYPE HAS NO CURVE' : 'NO PROFILE SELECTED'}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

StandbyBlock.propTypes = {
  profileName: PropTypes.string,
  profileError: PropTypes.any,
  onRetryProfile: PropTypes.func,
  curve: PropTypes.shape({
    pressure: PropTypes.string,
    flow: PropTypes.string,
    temperature: PropTypes.string,
  }),
};


function ManualSlider({ label, value, actual, unit, min, max, step, color, onChange, onEditingChange }) {
  const handleInput = useCallback(
    e => {
      onEditingChange(true);
      onChange(Number(e.currentTarget.value));
    },
    [onChange, onEditingChange]
  );
  const stopEditing = useCallback(() => onEditingChange(false), [onEditingChange]);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 7 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', gap: 10 }}>
        <span
          style={{
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 9,
            letterSpacing: '0.18em',
            color,
          }}
        >
          {label}
        </span>
        <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-dim)' }}>
          LIVE <span style={{ color: 'var(--dm-fg)' }}>{fmt(actual)}</span> / SET{' '}
          <span style={{ color: 'var(--dm-fg)' }}>{fmt(value)}</span> {unit}
        </span>
      </div>
      <input
        type='range'
        min={min}
        max={max}
        step={step}
        value={value}
        onInput={handleInput}
        onChange={handleInput}
        onPointerUp={stopEditing}
        onTouchEnd={stopEditing}
        onKeyUp={stopEditing}
        onBlur={stopEditing}
        style={{
          width: '100%',
          accentColor: color,
          cursor: 'pointer',
        }}
      />
    </div>
  );
}

ManualSlider.propTypes = {
  label: PropTypes.string,
  value: PropTypes.number,
  actual: PropTypes.number,
  unit: PropTypes.string,
  min: PropTypes.number,
  max: PropTypes.number,
  step: PropTypes.number,
  color: PropTypes.string,
  onChange: PropTypes.func,
  onEditingChange: PropTypes.func,
};

export function ManualConsole({
  active,
  finished,
  draft,
  selectedBean,
  dose,
  currentWeight,
  bluetoothConnected,
  scaleName,
  pressure,
  flow,
  temperature,
  controlLabels,
  isBeanDropdownOpen,
  beanOptions,
  loadingBeans,
  beanError,
  primaryAction,
  primaryActionAccent,
  primaryActionLabel,
  onBeanClick,
  onBeanSelect,
  onBeanRetry,
  onBeanDropdownClose,
  onDoseCommit,
  onEditingChange,
  onManualUpdate,
}) {
  const liveStatus = active ? 'LIVE CONTROL' : 'TEMP LIVE · PUMP STAGED';

  return (
    <>
      <div>
        <div
          style={{
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 9,
            letterSpacing: '0.18em',
            color: 'var(--dm-fg-dim)',
            marginBottom: 8,
          }}
        >
          MANUAL CONSOLE
        </div>
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
          {MANUAL_TARGET_OPTIONS.map(option => {
            const selected = draft.targetType === option.id;
            return (
              <button
                key={option.id}
                type='button'
                onClick={() => onManualUpdate({ targetType: option.id })}
                style={{
                  padding: '7px 10px',
                  borderRadius: 6,
                  cursor: 'pointer',
                  fontFamily: 'var(--dm-font-mono)',
                  fontSize: 9,
                  letterSpacing: '0.18em',
                  border: `1px solid ${selected ? 'color-mix(in srgb, var(--dm-accent) 60%, transparent)' : 'var(--dm-line)'}`,
                  background: selected ? 'color-mix(in srgb, var(--dm-accent) 12%, transparent)' : 'transparent',
                  color: selected ? 'var(--dm-accent)' : 'var(--dm-fg-dim)',
                }}
              >
                {option.label}
              </button>
            );
          })}
        </div>
      </div>

      <div
        style={{
          display: 'grid',
          gridTemplateColumns: '1fr auto 1fr auto 1fr',
          gap: 10,
          alignItems: 'center',
          padding: '12px 0',
          borderTop: '1px solid var(--dm-line)',
          borderBottom: '1px solid var(--dm-line)',
        }}
      >
        <div style={{ position: 'relative', minWidth: 0 }}>
          <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, letterSpacing: '0.18em', color: 'var(--dm-fg-dim)', marginBottom: 3 }}>
            BEAN
          </div>
          <button
            type='button'
            onClick={e => {
              e.stopPropagation();
              onBeanClick();
            }}
            style={{
              display: 'block',
              width: '100%',
              background: 'none',
              border: 'none',
              padding: 0,
              cursor: 'pointer',
              textAlign: 'left',
            }}
          >
            <span
              style={{
                display: 'block',
                fontFamily: 'var(--dm-font-display)',
                fontSize: 20,
                color: selectedBean ? 'var(--dm-fg)' : 'var(--dm-fg-faint)',
                fontWeight: 700,
                fontVariantNumeric: 'tabular-nums',
                lineHeight: 1.05,
                whiteSpace: 'nowrap',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                borderBottom: `1px dashed ${isBeanDropdownOpen ? 'var(--dm-accent)' : 'rgba(232,232,232,0.2)'}`,
              }}
            >
              {selectedBean || 'No bean selected'}
            </span>
          </button>
          {isBeanDropdownOpen && (
            <SelectDropdown
              label='SELECT BEAN'
              options={beanOptions}
              activeLabel={selectedBean}
              onSelect={onBeanSelect}
              loading={loadingBeans}
              error={beanError}
              onRetry={onBeanRetry}
              onClose={onBeanDropdownClose}
            />
          )}
        </div>
        <span style={{ fontFamily: 'var(--dm-font-display)', fontSize: 20, color: 'var(--dm-fg-faint)' }}>{'>'}</span>
        <EditableNumBlock
          label='DOSE'
          value={dose}
          unit='g'
          step={0.1}
          min={0.1}
          max={200}
          onCommit={onDoseCommit}
        />
        <span style={{ fontFamily: 'var(--dm-font-display)', fontSize: 20, color: 'var(--dm-fg-faint)' }}>{'>'}</span>
        <div>
          <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, letterSpacing: '0.18em', color: 'var(--dm-fg-dim)', marginBottom: 3 }}>
            SCALES
          </div>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 3 }}>
            <span
              style={{
                fontFamily: 'var(--dm-font-display)',
                fontSize: 28,
                color: bluetoothConnected ? 'var(--dm-good)' : 'var(--dm-fg-faint)',
                fontWeight: 700,
                fontVariantNumeric: 'tabular-nums',
                lineHeight: 1,
              }}
            >
              {bluetoothConnected ? fmt(currentWeight) : fmt(Number.NaN)}
            </span>
            <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-dim)' }}>g</span>
          </div>
          <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, color: 'var(--dm-fg-faint)', marginTop: 2 }}>
            {bluetoothConnected ? (scaleName || 'CONNECTED') : 'NOT CONNECTED'}
          </div>
        </div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 13 }}>
        <ManualSlider
          label={controlLabels.pressure}
          value={draft.pressure}
          actual={pressure}
          unit='bar'
          min={MANUAL_PRESSURE_MIN}
          max={MANUAL_PRESSURE_MAX}
          step={0.1}
          color='var(--dm-good)'
          onChange={pressure => onManualUpdate({ pressure: clampManualPressure(pressure) })}
          onEditingChange={onEditingChange}
        />
        <ManualSlider
          label={controlLabels.flow}
          value={draft.flow}
          actual={flow}
          unit='g/s'
          min={MANUAL_FLOW_MIN}
          max={MANUAL_FLOW_MAX}
          step={0.1}
          color='var(--dm-warn)'
          onChange={flow => onManualUpdate({ flow: clampManualFlow(flow) })}
          onEditingChange={onEditingChange}
        />
        <ManualSlider
          label='TEMPERATURE SETPOINT'
          value={draft.temperature}
          actual={temperature}
          unit='C'
          min={MANUAL_TEMP_MIN}
          max={MANUAL_TEMP_MAX}
          step={1}
          color='var(--dm-accent)'
          onChange={temperature => onManualUpdate({ temperature: clampManualTemperature(temperature) })}
          onEditingChange={onEditingChange}
        />
      </div>

      <div
        style={{
          display: 'grid',
          gridTemplateColumns: '1fr 160px',
          gap: 14,
          alignItems: 'stretch',
          marginTop: 'auto',
        }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', gap: 4 }}>
          <span
            style={{
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              letterSpacing: '0.18em',
              color: active ? 'var(--dm-good)' : 'var(--dm-fg-faint)',
            }}
          >
            {liveStatus}
          </span>
          <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-dim)' }}>
            {active ? 'SLIDERS SEND LIVE UPDATES' : 'PRESSURE/FLOW APPLY ON START'}
          </span>
        </div>
        <button
          type='button'
          onClick={primaryAction}
          style={getPrimaryActionButtonStyle({ active, finished, accent: primaryActionAccent })}
        >
          {primaryActionLabel}
        </button>
      </div>
    </>
  );
}

ManualConsole.propTypes = {
  active: PropTypes.bool,
  finished: PropTypes.bool,
  draft: PropTypes.object,
  selectedBean: PropTypes.string,
  dose: PropTypes.number,
  currentWeight: PropTypes.number,
  bluetoothConnected: PropTypes.bool,
  scaleName: PropTypes.string,
  pressure: PropTypes.number,
  flow: PropTypes.number,
  temperature: PropTypes.number,
  controlLabels: PropTypes.object,
  isBeanDropdownOpen: PropTypes.bool,
  beanOptions: PropTypes.array,
  loadingBeans: PropTypes.bool,
  beanError: PropTypes.string,
  primaryAction: PropTypes.func,
  primaryActionAccent: PropTypes.string,
  primaryActionLabel: PropTypes.string,
  onBeanClick: PropTypes.func,
  onBeanSelect: PropTypes.func,
  onBeanRetry: PropTypes.func,
  onBeanDropdownClose: PropTypes.func,
  onDoseCommit: PropTypes.func,
  onEditingChange: PropTypes.func,
  onManualUpdate: PropTypes.func,
};

function RingLegend({ color, label, value }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2, alignItems: 'center' }}>
      <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <span
          style={{
            width: 8,
            height: 8,
            borderRadius: 2,
            background: color,
            display: 'inline-block',
            flexShrink: 0,
          }}
        />
        <span
          style={{
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 9,
            letterSpacing: '0.12em',
            color: 'var(--dm-fg-dim)',
          }}
        >
          {label}
        </span>
      </span>
      <span
        style={{
          fontFamily: 'var(--dm-font-display)',
          fontSize: 13,
          color: 'var(--dm-fg)',
          fontWeight: 700,
        }}
      >
        {value}
      </span>
    </div>
  );
}

RingLegend.propTypes = { color: PropTypes.string, label: PropTypes.string, value: PropTypes.string };

// Editable NumBlock: big display number + ± stepper buttons, click-to-type
export function EditableNumBlock({ label, value, unit, hint, accent, step, min, max, onCommit, onTap, disabled = false, lockedHint }) {
  const [editing, setEditing] = useState(false);
  const inputRef = useRef(null);
  const triggerRef = useRef(null);
  const restoreTriggerFocusRef = useRef(false);

  const commit = useCallback(
    raw => {
      const normalized = String(raw).replace(unit, '').trim().replace(/,/g, '.');
      const parsed = parseQuantity(normalized);
      if (parsed !== null && parsed >= min && parsed <= max) onCommit(parsed);
      setEditing(false);
    },
    [unit, min, max, onCommit]
  );

  const adjust = useCallback(
    delta => {
      const next = Math.round((value + delta + Number.EPSILON) * 100) / 100;
      onCommit(Math.max(min, Math.min(max, next)));
    },
    [value, min, max, onCommit]
  );

  useEffect(() => {
    if (editing && inputRef.current) inputRef.current.select();
  }, [editing]);

  useEffect(() => {
    if (!editing && restoreTriggerFocusRef.current) {
      restoreTriggerFocusRef.current = false;
      triggerRef.current?.focus();
    }
  }, [editing]);

  const finishKeyboardEdit = useCallback(() => {
    restoreTriggerFocusRef.current = true;
    setEditing(false);
  }, []);

  return (
    <div style={{ position: 'relative' }}>
      <div
        style={{
          fontFamily: 'var(--dm-font-mono)',
          fontSize: 9,
          letterSpacing: '0.18em',
          color: 'var(--dm-fg-dim)',
          marginBottom: 3,
        }}
      >
        {disabled && lockedHint ? lockedHint : label}
      </div>

      {disabled ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span
            aria-disabled='true'
            title="Turn on 'Allow yield override' in Settings to edit per shot"
            style={{
              fontFamily: 'var(--dm-font-display)',
              fontSize: 28,
              color: 'var(--dm-fg-faint)',
              fontWeight: 700,
              fontVariantNumeric: 'tabular-nums',
              lineHeight: 1,
              cursor: 'default',
            }}
          >
            {typeof value === 'number' ? value.toFixed(1) : value}
          </span>
          <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-faint)' }}>
            {unit}
          </span>
        </div>
      ) : editing ? (
        <input
          ref={inputRef}
          type='text'
          aria-label={`Edit ${label}`}
          inputMode='decimal'
          defaultValue={value.toFixed(1)}
          onBlur={e => commit(e.target.value)}
          onKeyDown={e => {
            if (e.key === 'Enter') { restoreTriggerFocusRef.current = true; commit(e.target.value); }
            if (e.key === 'Escape') finishKeyboardEdit();
            if (e.key === 'ArrowUp') { e.preventDefault(); adjust(step); finishKeyboardEdit(); }
            if (e.key === 'ArrowDown') { e.preventDefault(); adjust(-step); finishKeyboardEdit(); }
          }}
          style={{
            display: 'block',
            width: '100%',
            maxWidth: '100%',
            minWidth: 0,
            boxSizing: 'border-box',
            fontFamily: 'var(--dm-font-display)',
            fontSize: 26,
            fontWeight: 700,
            color: accent || 'var(--dm-fg)',
            background: 'var(--dm-bg-2)',
            border: '1px solid var(--dm-accent)',
            borderRadius: 4,
            padding: '4px 6px',
            lineHeight: 1,
          }}
        />
      ) : (
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <button
            ref={triggerRef}
            type='button'
            aria-label={`Edit ${label}`}
            onClick={() => {
              // PRO-503: fire onTap BEFORE entering edit mode so a tap-away
              // without typing/Enter still records a fresh "confirmed" event
              // (mobile onBlur is unreliable). Distinct from onCommit — most
              // callers don't need this and it must not re-run their commit
              // side effects (e.g. DOSE's req:dose:set) on every open-tap.
              onTap?.();
              setEditing(true);
            }}
            style={{
              fontFamily: 'var(--dm-font-display)',
              fontSize: 28,
              color: accent || 'var(--dm-fg)',
              fontWeight: 700,
              fontVariantNumeric: 'tabular-nums',
              lineHeight: 1,
              cursor: 'text',
              border: 'none',
              borderBottom: '1px dashed rgba(232,232,232,0.2)',
              background: 'transparent',
              padding: 0,
            }}
          >
            {typeof value === 'number' ? value.toFixed(1) : value}
          </button>
          <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-dim)' }}>
            {unit}
          </span>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2, marginLeft: 2 }}>
            <button
              type='button'
              onClick={e => { e.stopPropagation(); adjust(step); }}
              style={stepperBtnStyle}
              aria-label={`Increase ${label}`}
            >
              ▲
            </button>
            <button
              type='button'
              onClick={e => { e.stopPropagation(); adjust(-step); }}
              style={stepperBtnStyle}
              aria-label={`Decrease ${label}`}
            >
              ▼
            </button>
          </div>
        </div>
      )}

      {hint && (
        <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, color: 'var(--dm-fg-faint)', marginTop: 2 }}>
          {hint}
        </div>
      )}
    </div>
  );
}

const stepperBtnStyle = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  width: 24,
  height: 24,
  padding: 0,
  background: 'var(--dm-bg-3)',
  border: '1px solid var(--dm-line)',
  borderRadius: 2,
  color: 'var(--dm-fg-dim)',
  fontSize: 10,
  cursor: 'pointer',
  lineHeight: 1,
};

const retryButtonStyle = {
  background: 'transparent',
  border: '1px solid var(--dm-line-strong)',
  borderRadius: 4,
  color: 'var(--dm-fg)',
  cursor: 'pointer',
  fontFamily: 'var(--dm-font-mono)',
  fontSize: 9,
  letterSpacing: '0.12em',
  padding: '4px 6px',
};

EditableNumBlock.propTypes = {
  label: PropTypes.string,
  value: PropTypes.number,
  unit: PropTypes.string,
  hint: PropTypes.string,
  accent: PropTypes.string,
  step: PropTypes.number,
  min: PropTypes.number,
  max: PropTypes.number,
  onCommit: PropTypes.func,
  onTap: PropTypes.func,
  disabled: PropTypes.bool,
  lockedHint: PropTypes.string,
};

// Format the current grind target for display. Mirrors ProcessControls'
// formatTarget: grindTarget === 1 means a volumetric (grams) target, otherwise
// a time (seconds) target. Returns null when no target is set so the caller can
// render a clean empty state ("No grind target").
function formatGrindTarget(grindTarget, grindTargetVolume, grindTargetDuration) {
  if (grindTarget === 1) {
    return grindTargetVolume > 0 ? `${Number(grindTargetVolume).toFixed(1)}g` : null;
  }
  return grindTargetDuration > 0 ? `${Math.round(grindTargetDuration / 1000)}s` : null;
}

// Inline dropdown for profile / bean selection
function SelectDropdown({ label, options, activeId, activeLabel, onSelect, loading, error, onRetry, onClose }) {
  return (
    <div
      style={{
        position: 'absolute',
        top: '100%',
        left: 0,
        zIndex: 50,
        marginTop: 4,
        minWidth: 240,
        background: 'var(--dm-bg-2)',
        border: '1px solid var(--dm-line-strong)',
        borderRadius: 8,
        boxShadow: '0 12px 32px rgba(0,0,0,0.7)',
        overflow: 'hidden',
      }}
      onClick={e => e.stopPropagation()}
    >
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          padding: '10px 12px',
          borderBottom: '1px solid var(--dm-line)',
        }}
      >
        <span
          style={{
            fontFamily: 'var(--dm-font-mono)',
            fontSize: 9,
            letterSpacing: '0.18em',
            color: 'var(--dm-fg-dim)',
          }}
        >
          {label}
        </span>
        <button
          type='button'
          onClick={onClose}
          style={{
            background: 'none',
            border: 'none',
            color: 'var(--dm-fg-dim)',
            cursor: 'pointer',
            fontSize: 12,
            lineHeight: 1,
            padding: '2px 4px',
          }}
          aria-label='Close'
        >
          ✕
        </button>
      </div>
      <div style={{ maxHeight: 220, overflowY: 'auto' }}>
        {loading ? (
          <div
            style={{
              padding: '14px 12px',
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              letterSpacing: '0.18em',
              color: 'var(--dm-fg-faint)',
            }}
          >
            LOADING...
          </div>
        ) : error ? (
          <div
            style={{
              padding: '14px 12px',
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              color: 'var(--dm-accent)',
            }}
          >
            <div role='status' aria-live='polite'>FAILED TO LOAD {label}</div>
            <button type='button' onClick={onRetry} style={{ ...retryButtonStyle, marginTop: 8 }}>RETRY</button>
          </div>
        ) : options.length === 0 ? (
          <div
            style={{
              padding: '14px 12px',
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              color: 'var(--dm-fg-faint)',
            }}
          >
            NO OPTIONS
          </div>
        ) : (
          options.map(opt => {
            const isActive = opt.id === activeId || opt.name === activeLabel;
            return (
              <button
                key={opt.id || opt.name}
                type='button'
                onClick={() => onSelect(opt)}
                style={{
                  display: 'block',
                  width: '100%',
                  padding: '9px 12px',
                  textAlign: 'left',
                  background: isActive ? 'color-mix(in srgb, var(--dm-accent) 12%, transparent)' : 'transparent',
                  border: 'none',
                  borderBottom: '1px solid var(--dm-line)',
                  fontFamily: 'var(--dm-font-mono)',
                  fontSize: 11,
                  color: isActive ? 'var(--dm-accent)' : 'var(--dm-fg)',
                  cursor: 'pointer',
                  letterSpacing: '0.04em',
                }}
              >
                {isActive && '▸ '}
                {opt.label || opt.name}
              </button>
            );
          })
        )}
      </div>
    </div>
  );
}

SelectDropdown.propTypes = {
  label: PropTypes.string,
  options: PropTypes.array,
  activeId: PropTypes.string,
  activeLabel: PropTypes.string,
  onSelect: PropTypes.func,
  loading: PropTypes.bool,
  error: PropTypes.string,
  onRetry: PropTypes.func,
  onClose: PropTypes.func,
};

function GraphLegend({ color, label, value, dashed }) {
  return (
    <span
      style={{
        display: 'inline-flex',
        alignItems: 'center',
        gap: 5,
        fontFamily: 'var(--dm-font-mono)',
        fontSize: 11,
        letterSpacing: '0.10em',
      }}
    >
      <span
        style={{
          width: 14,
          height: 0,
          borderTop: `2px ${dashed ? 'dashed' : 'solid'} ${color}`,
          display: 'inline-block',
          flexShrink: 0,
        }}
      />
      <span style={{ color: 'var(--dm-fg-dim)' }}>{label}</span>
      {value && <span style={{ color: 'var(--dm-fg)' }}>{value}</span>}
    </span>
  );
}

GraphLegend.propTypes = {
  color: PropTypes.string,
  label: PropTypes.string,
  value: PropTypes.string,
  dashed: PropTypes.bool,
};

export function getRecipeGridStyle(isMobile) {
  return {
    display: 'grid',
    gridTemplateColumns: isMobile ? '1fr' : '1fr auto 1fr auto 1fr auto 1fr',
    gap: 10,
    alignItems: 'center',
    padding: '12px 0',
    borderTop: '1px solid var(--dm-line)',
    borderBottom: '1px solid var(--dm-line)',
  };
}

export const graphLegendRowStyle = {
  display: 'flex',
  gap: 16,
  alignItems: 'center',
  flexWrap: 'wrap',
};

// ── Graph SVG ───────────────────────────────────────────────────────────────

const GW = 1200, GH = 240, GP_L = 44, GP_R = 44, GP_T = 12, GP_B = 22;
const G_INNER_W = GW - GP_L - GP_R;
const G_INNER_H = GH - GP_T - GP_B;

// Left axis: pressure 0–12 bar, flow 0–4 g/s (shared scale via normalisation)
function pToY(p) {
  return GP_T + G_INNER_H - (p / 12) * G_INNER_H;
}
function fToY(f) {
  return GP_T + G_INNER_H - (f / 4) * G_INNER_H;
}
// Right axis: temperature 0–110 °C
const TEMP_GRAPH_MAX = 110;
function tempToY(t) {
  return GP_T + G_INNER_H - (Math.max(0, t) / TEMP_GRAPH_MAX) * G_INNER_H;
}

function buildGraphPaths(entries) {
  if (entries.length < 2) return null;

  const first = entries[0].timestamp.getTime();
  const last = entries[entries.length - 1].timestamp.getTime();
  const duration = Math.max(last - first, 1);

  function tToX(t) {
    return GP_L + ((t - first) / duration) * G_INNER_W;
  }

  let pd = '', fd = '', tpd = '', tempd = '';
  let prevHadTarget = false;

  entries.forEach((h, i) => {
    const x = tToX(h.timestamp.getTime()).toFixed(1);
    const cmd = i === 0 ? 'M' : 'L';
    pd    += `${cmd}${x} ${pToY(h.currentPressure || 0).toFixed(1)} `;
    fd    += `${cmd}${x} ${fToY(Math.min(h.currentFlow || 0, 4)).toFixed(1)} `;
    tempd += `${cmd}${x} ${tempToY(h.currentTemperature || 0).toFixed(1)} `;
    if (h.targetPressure > 0) {
      const ty = pToY(h.targetPressure).toFixed(1);
      tpd += `${prevHadTarget ? 'L' : 'M'}${x} ${ty} `;
      prevHadTarget = true;
    } else {
      prevHadTarget = false;
    }
  });

  const curX = tToX(last).toFixed(1);
  const lastEntry = entries[entries.length - 1];
  const curPY = pToY(lastEntry.currentPressure || 0).toFixed(1);

  return { pd, fd, tpd, tempd, curX, curPY };
}

// ── Main component ──────────────────────────────────────────────────────────

export default function DashboardMerged({ navOpen = false, onNavToggle }) {
  const api = useContext(ApiServiceContext);
  const s = status.value;
  const processInfo = s.process;
  const active = !!processInfo?.a;
  const finished = !!processInfo?.e && !active;
  const history = machine.value.history;
  const connected = machine.value.connected;

  const [isFlushing, setIsFlushing] = useState(false);
  const lastProcessTypeRef = useRef(null);
  const isGrind = s.mode === 4;
  const brew = s.mode === 1;
  const mode = s.mode ?? 0;
  const { isGrindAvailable } = useGrindSettings();
  const isManualAvailable = machine.value.capabilities.pressure === true;
  const isSteamMode = mode === MODE_STEAM;
  const isManualMode = mode === MODE_MANUAL;
  // PRO-426: standby (mode 0, non-manual) swaps the live-shot widgets (target
  // bars + yield bar) for a standby block (grinder + grind setting + selected
  // profile mini-curve).
  const isStandby = mode === MODE_STANDBY;
  const processKind = getProcessKindForMode(mode, isGrindAvailable, isManualAvailable);
  const availableModes = useMemo(
    () => getAvailableModeOptions(isGrindAvailable, isManualAvailable),
    [isGrindAvailable, isManualAvailable]
  );

  const [isMobile, setIsMobile] = useState(() => window.innerWidth < 640);
  useEffect(() => {
    const mq = window.matchMedia('(max-width: 639px)');
    const handler = e => setIsMobile(e.matches);
    mq.addEventListener('change', handler);
    return () => mq.removeEventListener('change', handler);
  }, []);

  const { autoSteamEnabled, toggleAutoSteam } = useAutoSteam();
  const { standbyOnBrewEnabled, toggleStandbyOnBrew } = useStandbyOnBrew();
  const standbyButtonState = computeStandbyOnBrewButtonState({ standbyOnBrewEnabled, autoSteamEnabled });
  // Track whether the *last* shot was started in brew mode, captured while active.
  // We cannot rely on `brew` being true when `active` flips false because the
  // controller transitions mode to STANDBY before the WebSocket delivers active=false.
  const wasActiveRef = useRef(false);
  const lastActiveWasBrewRef = useRef(false);
  useEffect(() => {
    if (active) {
      wasActiveRef.current = true;
      lastActiveWasBrewRef.current =
        lastProcessTypeRef.current === 'brew' || lastProcessTypeRef.current === 'manual';
      return;
    }
    if (!wasActiveRef.current) return;
    wasActiveRef.current = false;
    setIsFlushing(false);
    if (shouldFireAutoSteamOnStop({ lastActiveWasBrew: lastActiveWasBrewRef.current, autoSteamEnabled })) {
      try { api.send({ tp: 'req:change-mode', mode: MODE_STEAM }); } catch {}
      // PRO-421: auto-steam fires exactly once per brew shot. Clearing the flag
      // (and the underlying last-process type) prevents the effect from firing
      // AGAIN when the auto-steamed session is later stopped: without this, the
      // steam session re-captures lastActiveWasBrewRef === true (lastProcessType
      // is still 'brew'), so pressing "Stop Steam" would re-send change-mode:2
      // right after the STANDBY and bounce the machine back to Steam.
      lastActiveWasBrewRef.current = false;
      lastProcessTypeRef.current = null;
    } else if (
      shouldFireStandbyOnStop({
        lastActiveWasBrew: lastActiveWasBrewRef.current,
        standbyOnBrewEnabled,
        autoSteamEnabled,
      })
    ) {
      // PRO-545: standby-on-brew drops the machine to Standby after a brew shot.
      // Auto-steam takes priority (the branch above / the predicate's
      // !autoSteamEnabled guard), so this only fires when auto-steam is off.
      // This is the FIRST change-mode of the transition (not a re-assert), so it
      // passes through StandbyReassertPolicy/ChangeModeDeferPolicy untouched.
      // PRO-587: mark this transition `auto: true` so the firmware recognizes it
      // as an AUTOMATIC post-shot standby (not an explicit human stop) and lets
      // it ride the same settle window as auto-steam — the BLE scale stays
      // connected until extended-recording closes so the final drips reach the
      // recorded yield. An explicit STANDBY (physical button, web Standby button,
      // HomeKit) sends a bare `req:change-mode` with no `auto` flag and keeps its
      // instant-stop behavior (PRO-265).
      // Same exactly-once discipline as auto-steam: clear the refs so the effect
      // does not re-fire on subsequent inactive transitions of the same shot.
      try { api.send({ tp: 'req:change-mode', mode: MODE_STANDBY, auto: true }); } catch {}
      lastActiveWasBrewRef.current = false;
      lastProcessTypeRef.current = null;
    }
  }, [active, autoSteamEnabled, standbyOnBrewEnabled, api, setIsFlushing]);

  const actions = useProcessActions(api, isGrind, setIsFlushing, lastProcessTypeRef, processKind);
  const [manualDraft, setManualDraft] = useState(() => manualDraftFromStatus(s));
  const [isManualEditing, setIsManualEditing] = useState(false);
  const [manualDraftDirty, setManualDraftDirty] = useState(false);
  const wasManualModeRef = useRef(false);

  useEffect(() => {
    if (!isManualMode) {
      setManualDraftDirty(false);
    }
  }, [isManualMode]);

  useEffect(() => {
    if (isManualEditing || manualDraftDirty) return;
    setManualDraft(manualDraftFromStatus(s));
  }, [
    isManualEditing,
    manualDraftDirty,
    s.manualTargetType,
    s.manualPressure,
    s.manualFlow,
    s.manualTemperature,
    s.targetPressure,
    s.targetFlow,
    s.targetTemperature,
  ]);

  const sendManualPayload = useCallback(
    (draft, { temperatureOnly = false } = {}) => {
      const payload = {
        tp: 'req:manual:update',
        temperature: Math.round(draft.temperature),
      };
      if (!temperatureOnly) {
        payload.targetType = draft.targetType;
        payload.pressure = draft.pressure;
        payload.flow = draft.flow;
      }
      api.send(payload);
    },
    [api]
  );

  const sendManualUpdate = useCallback(
    partial => {
      setManualDraft(current => {
        const next = {
          targetType: normalizeManualTargetType(partial.targetType ?? current.targetType),
          pressure: clampManualPressure(partial.pressure ?? current.pressure),
          flow: clampManualFlow(partial.flow ?? current.flow),
          temperature: clampManualTemperature(partial.temperature ?? current.temperature),
        };
        const shouldSend = !isFlushing && shouldSendManualUpdate({ active, isManualMode, partial });
        const temperatureOnly = !active && Object.prototype.hasOwnProperty.call(partial ?? {}, 'temperature');
        setManualDraftDirty(currentDirty => currentDirty || shouldKeepManualDraftDirty({ active, partial }));
        if (shouldSend) {
          try { sendManualPayload(next, { temperatureOnly }); } catch {}
        }
        return next;
      });
    },
    [active, isFlushing, isManualMode, sendManualPayload]
  );

  useEffect(() => {
    if (!isManualMode || !isManualAvailable) {
      wasManualModeRef.current = false;
      return;
    }
    if (!wasManualModeRef.current) {
      try { sendManualPayload(manualDraft, { temperatureOnly: true }); } catch {}
    }
    wasManualModeRef.current = true;
  }, [isManualAvailable, isManualMode, manualDraft, sendManualPayload]);

  // PRO-426: also fetch profile data in standby (mode 0) so the standby shot
  // card can render the selected-profile mini-curve. The `brew` gate here drives
  // the profiles-LIST fetch; the selected profile itself is fetched off
  // selectedProfileId regardless of mode.
  const { profileData, error: profileDataError, retry: retryProfileData } = useProfileData(
    api,
    brew || s.mode === MODE_STANDBY,
    s.selectedProfileId
  );

  // Connected scale name
  const [scaleName, setScaleName] = useState(null);
  useEffect(() => {
    if (!s.bluetoothConnected) { setScaleName(null); return; }
    authenticatedFetch('/api/scales/info')
      .then(r => r.json())
      .then(d => setScaleName(d?.name || null))
      .catch(() => setScaleName(null));
  }, [s.bluetoothConnected]);

  // Dose — device-authoritative (PRO-226). The device broadcasts `dg` in
  // evt:status (mapped to s.doseGrams); localStorage is an offline-only cache
  // that seeds the UI pre-connection and is mirrored on every commit.
  const [cachedDose, setCachedDose] = useState(() => {
    try { return parseQuantity(localStorage.getItem(DOSE_KEY)) ?? DEFAULT_DOSE; } catch { return DEFAULT_DOSE; }
  });
  const dose =
    connected && Number.isFinite(s.doseGrams) ? s.doseGrams : cachedDose;
  const setDose = useCallback(val => {
    // Clamp to the on-screen range; firmware re-validates/clamps to [0.1, 200].
    const v = Math.max(0.1, Math.min(200, val));
    setCachedDose(v);
    try { localStorage.setItem(DOSE_KEY, String(v)); } catch {}
    try {
      // Fire-and-forget: device echoes the value back via evt:status `dg`.
      api.send({ tp: 'req:dose:set', grams: v });
    } catch (error) {
      console.error('Failed to send dose:', error);
    }
  }, [api]);

  // Manual grinder-dial setting — device-authoritative (PRO-603). The device
  // broadcasts `mg` in evt:status (mapped to s.manualGrindSetting); localStorage
  // is an offline-only seed that primes the UI pre-connection and is mirrored on
  // every commit. When connected, the device value wins so browsers stay in sync.
  const [cachedManualGrind, setCachedManualGrind] = useState(() => {
    try { return parseQuantity(localStorage.getItem(MANUAL_GRIND_SETTING_STORAGE_KEY)) ?? DEFAULT_MANUAL_GRIND; } catch { return DEFAULT_MANUAL_GRIND; }
  });
  const manualGrind =
    connected && Number.isFinite(s.manualGrindSetting) ? s.manualGrindSetting : cachedManualGrind;
  const recordActiveShotManualGrind = useActiveShotManualGrindRecorder(api);
  const setManualGrind = useCallback(val => {
    // Permissive range: grinder dials vary enormously (0–10 with decimals on a
    // Niche, 0–40+ clicks on a DF64, arbitrary worm-drive numbers), so clamp
    // only to a wide, neutral [0, 100] band rather than any grinder-specific
    // scale. step=0.1 supports decimal dials. Firmware re-validates/clamps.
    const v = Math.max(0, Math.min(100, val));
    setCachedManualGrind(v);
    try { localStorage.setItem(MANUAL_GRIND_SETTING_STORAGE_KEY, String(v)); } catch {}
    try {
      // Fire-and-forget: device echoes the value back via evt:status `mg`.
      api.send({ tp: 'req:manual-grind:set', value: v });
    } catch (error) {
      console.error('Failed to send manual grind:', error);
    }
    // Record for Shot Notes pre-fill (per-profile, per-browser; PRO-431/439).
    // Separate concern from the cross-browser sync above — keep as-is. Skip 0 —
    // that is the "not set" sentinel and should not shadow the machine target label.
    if (v > 0) {
      const st = machine.value.status;
      recordManualGrindSetting({
        profileId: st.selectedProfileId,
        profileLabel: st.selectedProfile,
        grindSetting: String(v),
      });
      recordActiveShotManualGrind(String(v));
    }
  }, [api, recordActiveShotManualGrind]);

  // Auto-attach dose, bean, and the manual grind value to shot notes as soon as
  // the shot becomes active. Dose/bean overwrite (read-merge-write); grind is
  // fill-only-if-absent so an in-shot commit or manual Shot Notes edit wins.
  useShotDoseRecorder(api, dose, manualGrind);

  // Target yield — profile-authoritative (CAR-375). The displayed yield always
  // tracks the active profile's volumetric target (s.brewTargetVolume),
  // reseeding whenever the profile changes. localStorage is intentionally NOT
  // used as the seed source to avoid stale-value bugs. When override is allowed
  // AND the active profile is volumetric the field is editable and commits send
  // req:change-brew-target; otherwise it is read-only. Non-volumetric profiles
  // keep the disabled/locked treatment regardless of the override setting,
  // because Controller::setBrewTarget() ignores a brew target for them.
  const yieldEditable = computeYieldEditable({
    allowYieldOverride: s.allowYieldOverride,
    brewTarget: s.brewTarget,
    bluetoothConnected: s.bluetoothConnected,
  });
  const yieldLockReason = getYieldLockReason({
    allowYieldOverride: s.allowYieldOverride,
    brewTarget: s.brewTarget,
    bluetoothConnected: s.bluetoothConnected,
  });
  const [yieldTarget, setYieldTargetState] = useState(() => s.brewTargetVolume || DEFAULT_YIELD);

  // Reseed to the active profile's target on profile change (and whenever the
  // device's broadcast target changes). Applies in both editable and locked
  // states so a custom value never silently carries across a profile switch.
  // When the active profile has no volumetric target (brewTargetVolume <= 0,
  // e.g. a time/pressure profile), fall back to DEFAULT_YIELD rather than
  // holding the previous profile's number — the device ignores yield there
  // anyway, so a stale "locked" value would be misleading.
  useEffect(() => {
    setYieldTargetState(s.brewTargetVolume > 0 ? s.brewTargetVolume : DEFAULT_YIELD);
  }, [s.selectedProfileId, s.brewTargetVolume]);

  const setYield = useCallback(val => {
    if (!yieldEditable) return;
    const v = Math.max(5, Math.min(120, val));
    setYieldTargetState(v);
    try { api.send({ tp: 'req:change-brew-target', target: v }); } catch {}
  }, [api, yieldEditable]);
  // Profile dropdown
  const [activeDropdown, setActiveDropdown] = useState(null); // 'profile' | 'bean' | null
  const [profileOptions, setProfileOptions] = useState([]);
  const [beanOptions, setBeanOptions] = useState([]);
  const [loadingProfiles, setLoadingProfiles] = useState(false);
  const [loadingBeans, setLoadingBeans] = useState(false);
  const [profileError, setProfileError] = useState(null);
  const [beanError, setBeanError] = useState(null);
  const [grinderOptions, setGrinderOptions] = useState([]);
  const [loadingGrinders, setLoadingGrinders] = useState(false);
  const [grinderError, setGrinderError] = useState(null);
  const profilesLoadedRef = useRef(false);
  const beansLoadedRef = useRef(false);
  const grindersLoadedRef = useRef(false);

  const loadProfiles = useCallback(async () => {
    if (profilesLoadedRef.current) return;
    profilesLoadedRef.current = true;
    setLoadingProfiles(true); setProfileError(null);
    try {
      const res = await api.request({ tp: 'req:profiles:list' });
      setProfileOptions((res?.profiles || []).filter(p => !p.archived));
    } catch { setProfileError('Failed to load'); profilesLoadedRef.current = false; }
    finally { setLoadingProfiles(false); }
  }, [api]);

  const loadBeans = useCallback(async () => {
    if (beansLoadedRef.current) return;
    beansLoadedRef.current = true;
    setLoadingBeans(true); setBeanError(null);
    try {
      const beans = await listBeans(api);
      setBeanOptions((beans || []).filter(b => !b.archived));
    } catch { setBeanError('Failed to load'); beansLoadedRef.current = false; }
    finally { setLoadingBeans(false); }
  }, [api]);

  const loadGrinders = useCallback(async () => {
    if (grindersLoadedRef.current) return;
    grindersLoadedRef.current = true;
    setLoadingGrinders(true); setGrinderError(null);
    try {
      const grinders = await listGrinders(api);
      // listGrinders returns an ordered array of name strings; SelectDropdown
      // reads `opt.name`/`opt.label` and keys on `opt.id || opt.name`.
      setGrinderOptions((grinders || []).map(name => ({ name })));
    } catch { setGrinderError('Failed to load'); grindersLoadedRef.current = false; }
    finally { setLoadingGrinders(false); }
  }, [api]);

  const openProfileDropdown = useCallback(() => {
    loadProfiles();
    setActiveDropdown(d => d === 'profile' ? null : 'profile');
  }, [loadProfiles]);

  const openBeanDropdown = useCallback(() => {
    loadBeans();
    setActiveDropdown(d => d === 'bean' ? null : 'bean');
  }, [loadBeans]);

  const openGrinderDropdown = useCallback(() => {
    loadGrinders();
    setActiveDropdown(d => d === 'grinder' ? null : 'grinder');
  }, [loadGrinders]);

  const handleProfileSelect = useCallback(async opt => {
    try {
      await api.request({ tp: 'req:profiles:select', id: opt.id });
      setActiveDropdown(null);
    } catch { console.error('profile select failed'); }
  }, [api]);

  const handleBeanSelect = useCallback(opt => {
    try {
      api.send({ tp: 'req:beans:select', name: opt.name });
      recordBeanSelection({
        profileId: machine.value.status.selectedProfileId,
        profileLabel: machine.value.status.selectedProfile,
        bean: opt,
      });
      setActiveDropdown(null);
    } catch { console.error('bean select failed'); }
  }, [api]);

  const handleGrinderSelect = useCallback(opt => {
    try {
      // Persist the selection on the device (firmware handler from PRO-424) and
      // record a per-browser selection event so a subsequent shot's notes
      // pre-fill the grinder + current grind-setting label. Mirrors
      // handleBeanSelect / recordBeanSelection.
      api.send({ tp: 'req:grinders:select', name: opt.name });
      const st = machine.value.status;
      recordGrinderSelection({
        profileId: st.selectedProfileId,
        profileLabel: st.selectedProfile,
        grinder: opt.name,
        grindSetting: formatGrindTarget(st.grindTarget, st.grindTargetVolume, st.grindTargetDuration),
      });
      setActiveDropdown(null);
    } catch { console.error('grinder select failed'); }
  }, [api]);

  // Close dropdown when clicking outside
  useEffect(() => {
    if (!activeDropdown) return;
    const handler = () => setActiveDropdown(null);
    document.addEventListener('click', handler);
    return () => document.removeEventListener('click', handler);
  }, [activeDropdown]);

  // Track brew start time for graph range
  const brewStartRef = useRef(null);
  useEffect(() => {
    if (active && brewStartRef.current === null) {
      brewStartRef.current = new Date();
    } else if (!active && !finished) {
      brewStartRef.current = null;
    }
  }, [active, finished]);

  // Sensor values with safe fallbacks
  const pressure = s.currentPressure || 0;
  const flowVal = s.currentFlow || 0;
  const tempVal = s.currentTemperature || 0;
  // Use ?? (not ||) so a legitimate 0 target (e.g. a flow-targeted first phase
  // with pressure: 0, or a pressure-targeted phase with flow: 0) is preserved
  // rather than being replaced by the default. Firmware sends null when the
  // target is genuinely unavailable.
  const targetPressure = isManualMode ? manualDraft.pressure : s.targetPressure ?? 9;
  const targetFlow = isManualMode ? manualDraft.flow : s.targetFlow ?? 2;
  const targetTemp = isManualMode ? manualDraft.temperature : s.targetTemperature || 93;
  const currentWeight = s.currentWeight || 0;
  const temperatureRing = getTemperatureRingMetrics({ mode, tempVal, targetTemp });
  const manualControlLabels = getManualControlLabels(manualDraft.targetType);

  // Target yield/time
  const targetWeight = yieldTarget;
  const targetShotSecs = s.brewTargetDuration > 0
    ? Math.round(s.brewTargetDuration / 1000)
    : 32;

  // Elapsed time
  const elapsedSecs = Math.round((processInfo?.e || 0) / 1000);

  // Current phase label
  const currentPhaseLabel = processInfo?.l?.trim() || null;

  // Phase list from profile (named phases only — no fake fallback)
  const profilePhases = useMemo(() => {
    if (Array.isArray(profileData?.phases) && profileData.phases.length > 0) {
      return profileData.phases.map(p => p.name || 'Phase');
    }
    return [];
  }, [profileData]);

  // PRO-426: dark-themed selected-profile mini-curve for the standby block.
  // Built off the same phase schema the ExtendedProfileChart consumes, but as a
  // hand-rolled SVG (no Chart.js). Fixed viewBox; the SVG stretches to fit.
  const standbyCurve = useMemo(
    () =>
      buildStandbyProfileCurve(profileData, {
        width: STANDBY_CURVE_VB_W,
        height: STANDBY_CURVE_VB_H,
        padding: 3,
      }),
    [profileData]
  );

  // Index of current phase in profile list (case-insensitive)
  const currentPhaseIdx = currentPhaseLabel && profilePhases.length > 0
    ? profilePhases.findIndex(p => p.toLowerCase() === currentPhaseLabel.toLowerCase())
    : -1;

  // When no profile phases exist or no name matched, fall back to showing the
  // raw machine label as the only chip (only while active)
  const showFallbackChip = active && currentPhaseLabel && (profilePhases.length === 0 || currentPhaseIdx === -1);

  // Mode selector
  const onModeSelect = useCallback(
    idx => {
      try {
        api.send({ tp: 'req:change-mode', mode: idx });
      } catch {}
    },
    [api]
  );

  // Ring geometry
  const RING_SIZE = isMobile ? 200 : 300;
  const cx = RING_SIZE / 2;
  const cy = RING_SIZE / 2;
  const stroke = 16;
  const gap = stroke + 6;
  const rOuter = RING_SIZE / 2 - stroke / 2 - 4;
  const rMid = rOuter - gap;
  const rInner = rMid - gap;

  // Graph history slice
  const graphHistory = useMemo(() => {
    if (!history || history.length === 0) return [];
    if (brewStartRef.current) {
      const start = brewStartRef.current;
      return history.filter(h => h.timestamp >= start);
    }
    const cutoff = new Date(Date.now() - 60000);
    return history.filter(h => h.timestamp >= cutoff);
  }, [history]);

  const graphPaths = useMemo(() => buildGraphPaths(graphHistory), [graphHistory]);

  // Boiler pill: green if stable, amber if heating.
  const isHeating = getBoilerHeatingState({ mode, active, finished, targetTemp, tempVal });
  // STEAM flashes yellow; BREW and WATER flash red
  const heatingColor = mode === 2 ? 'var(--dm-warn)' : 'var(--dm-accent)';
  const showProcessTimer = (active || finished) && !isSteamMode;
  const tempRingColor = temperatureRing.color;
  const steamStatusLabel = targetTemp > 0 && tempVal < targetTemp
    ? `PREHEATING · TGT ${fmt(targetTemp)}°`
    : `STEAM · TGT ${fmt(targetTemp)}°`;
  const primaryActionState = getPrimaryActionState({ active, finished, mode, connected, isGrindAvailable, isManualAvailable });
  const primaryActionLabel = primaryActionState.label;
  const primaryActionAccent = primaryActionState.accent;
  const readinessSummary = getReadinessSummary({
    mode,
    connected,
    bluetoothConnected: s.bluetoothConnected,
    selectedProfile: s.selectedProfile,
    wakeAvailable: primaryActionLabel === 'WAKE' && connected,
  });
  const primaryAction = () => {
    if (Object.prototype.hasOwnProperty.call(primaryActionState, 'processKind')) {
      lastProcessTypeRef.current = primaryActionState.processKind;
    }
    if (primaryActionState.action === 'change-mode') {
      api.send({ tp: 'req:change-mode', mode: primaryActionState.mode });
    } else if (primaryActionState.action === 'deactivate') {
      actions.deactivate();
    } else if (primaryActionState.action === 'clear') {
      actions.clear();
    } else if (primaryActionState.action === 'start-process') {
      if (isManualMode) {
        try {
          sendManualPayload(manualDraft);
          setManualDraftDirty(false);
        } catch {}
      }
      actions.activate();
    }
  };

  return (
    <div
      className='dm-shell'
      style={{
        display: 'flex',
        flexDirection: 'column',
        minHeight: 0,
        borderRadius: 16,
        overflow: 'hidden',
      }}
    >
      {/* Top header: branding + nav links */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          padding: '10px 16px',
          borderBottom: '1px solid var(--dm-line)',
          background: 'var(--dm-bg-2)',
          gap: 12,
        }}
      >
        <button
          type='button'
          onClick={onNavToggle}
          aria-expanded={navOpen}
          aria-controls='app-navigation-drawer'
          aria-label={navOpen ? 'Close navigation menu' : 'Open navigation menu'}
          style={{ display: 'inline-flex', alignItems: 'center', gap: 10, background: 'none', border: 'none', padding: 0, cursor: 'pointer' }}
        >
          <span style={{ display: 'grid', placeItems: 'center', width: 32, height: 32, borderRadius: 10, background: 'var(--dm-accent)', color: '#fff', flexShrink: 0 }}>
            <span style={{ fontFamily: 'var(--dm-font-display)', fontSize: 16, fontWeight: 700 }}>G</span>
          </span>
          <span style={{ lineHeight: 1.2 }}>
            <span style={{ fontFamily: 'var(--dm-font-display)', fontSize: 18, fontWeight: 700, color: 'var(--dm-fg)', letterSpacing: '0.06em' }}>
              GAGGI<span style={{ fontWeight: 400 }}>MATE</span>
            </span>
            <span style={{ display: 'block', fontFamily: 'var(--dm-font-mono)', fontSize: 8, letterSpacing: '0.2em', color: 'var(--dm-fg-faint)', textTransform: 'uppercase' }}>
              Live espresso control
            </span>
          </span>
        </button>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <a
            aria-label='github'
            rel='noopener noreferrer'
            href='https://github.com/jniebuhr/gaggimate'
            target='_blank'
            style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 30, height: 30, borderRadius: 8, background: 'transparent', color: 'var(--dm-fg-dim)', border: 'none', cursor: 'pointer', textDecoration: 'none' }}
          >
            <FontAwesomeIcon icon={faGithub} style={{ fontSize: 16 }} />
          </a>
          <a
            aria-label='discord'
            rel='noopener noreferrer'
            href='https://discord.gaggimate.eu/'
            target='_blank'
            style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 30, height: 30, borderRadius: 8, background: 'transparent', color: 'var(--dm-fg-dim)', border: 'none', cursor: 'pointer', textDecoration: 'none' }}
          >
            <FontAwesomeIcon icon={faDiscord} style={{ fontSize: 16 }} />
          </a>
          <button
            type='button'
            onClick={onNavToggle}
            aria-expanded={navOpen}
            aria-controls='app-navigation-drawer'
            aria-label={navOpen ? 'Close navigation menu' : 'Open navigation menu'}
            style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 30, height: 30, borderRadius: 8, background: 'transparent', color: 'var(--dm-fg-dim)', border: 'none', cursor: 'pointer' }}
          >
            <svg fill='currentColor' viewBox='0 0 20 20' style={{ width: 18, height: 18 }}>
              <path fillRule='evenodd' d='M3 5a1 1 0 011-1h12a1 1 0 110 2H4a1 1 0 01-1-1zM3 10a1 1 0 011-1h12a1 1 0 110 2H4a1 1 0 01-1-1zM3 15a1 1 0 011-1h12a1 1 0 110 2H4a1 1 0 01-1-1z' clipRule='evenodd' />
            </svg>
          </button>
        </div>
      </div>

      {/* Sub-header: mode rail + status pills + phase strip */}
      <div role='status' aria-live='polite' style={{ padding: '8px 16px', fontFamily: 'var(--dm-font-mono)', fontSize: 9, color: 'var(--dm-fg-dim)' }}>
        {readinessSummary}
      </div>
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          padding: '10px 16px',
          borderBottom: '1px solid var(--dm-line)',
          gap: 12,
          flexWrap: 'wrap',
          background: 'var(--dm-bg-1)',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
          <ModeRail active={mode} modes={availableModes} onSelect={onModeSelect} />
          {(brew || isManualMode) && !active && !finished && (
            <button
              type='button'
              onClick={actions.startFlush}
              title='Run a timed water flush'
              style={{
                background: 'transparent',
                color: 'var(--dm-fg-faint)',
                border: '1px solid var(--dm-line)',
                fontFamily: 'var(--dm-font-mono)',
                fontSize: 9,
                letterSpacing: '0.18em',
                padding: '4px 8px',
                borderRadius: 6,
                cursor: 'pointer',
                whiteSpace: 'nowrap',
              }}
            >
              ~ FLUSH
            </button>
          )}
          {(brew || isManualMode) && (
            <button
              type='button'
              onClick={toggleAutoSteam}
              title='Auto-switch to steam mode when brew ends'
              className={autoSteamEnabled ? 'nd-chip-danger-active' : ''}
              style={{
                ...(autoSteamEnabled ? {} : {
                  background: 'transparent',
                  color: 'var(--dm-fg-faint)',
                  border: '1px solid var(--dm-line)',
                }),
                fontFamily: 'var(--dm-font-mono)',
                fontSize: 9,
                letterSpacing: '0.18em',
                padding: '4px 8px',
                borderRadius: 6,
                cursor: 'pointer',
                transition: 'background 0.15s, border-color 0.15s, color 0.15s',
                whiteSpace: 'nowrap',
              }}
            >
              {autoSteamEnabled ? '~ AUTO STEAM ON' : '~ AUTO STEAM'}
            </button>
          )}
          {(brew || isManualMode) && (
            <button
              type='button'
              onClick={standbyButtonState.disabled ? undefined : toggleStandbyOnBrew}
              disabled={standbyButtonState.disabled}
              title={
                standbyButtonState.disabled
                  ? 'Disabled while Auto Steam is on (Auto Steam takes priority)'
                  : 'Auto-switch to standby when brew ends'
              }
              className={standbyButtonState.armed ? 'nd-chip-danger-active' : ''}
              style={{
                ...(standbyButtonState.armed ? {} : {
                  background: 'transparent',
                  color: 'var(--dm-fg-faint)',
                  border: '1px solid var(--dm-line)',
                }),
                fontFamily: 'var(--dm-font-mono)',
                fontSize: 9,
                letterSpacing: '0.18em',
                padding: '4px 8px',
                borderRadius: 6,
                cursor: standbyButtonState.disabled ? 'not-allowed' : 'pointer',
                opacity: standbyButtonState.disabled ? 0.4 : 1,
                transition: 'background 0.15s, border-color 0.15s, color 0.15s',
                whiteSpace: 'nowrap',
              }}
            >
              {standbyButtonState.armed ? '~ STANDBY ON' : '~ STANDBY'}
            </button>
          )}
        </div>
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: 14,
            flexWrap: 'wrap',
          }}
        >
          <StatusPill
            ledClass={connected ? 'dm-led--ok' : 'dm-led--warm'}
            label='ONLINE'
            value={connected ? 'WIFI' : 'OFFLINE'}
          />
          <span
            style={{ width: 1, height: 14, background: 'var(--dm-line)', display: 'inline-block' }}
          />
          <span
            style={{ width: 1, height: 14, background: 'var(--dm-line)', display: 'inline-block' }}
          />
          <div style={{ display: 'flex', gap: 5, flexWrap: 'wrap' }}>
            {showFallbackChip ? (
              <PhaseChip key='__fallback' label={currentPhaseLabel} isActive isDone={false} />
            ) : (
              profilePhases.map((p, i) => (
                <PhaseChip
                  key={p}
                  label={p}
                  isActive={active && i === currentPhaseIdx}
                  isDone={active && currentPhaseIdx > 0 && i < currentPhaseIdx}
                />
              ))
            )}
          </div>
        </div>
      </div>

      {/* Top row: ring | recipe + targets */}
      <div
        style={{
          display: 'grid',
          gridTemplateColumns: isMobile ? '1fr' : `${RING_SIZE + 48}px 1fr`,
          borderBottom: '1px solid var(--dm-line)',
        }}
      >
        {/* Ring panel */}
        <div
          style={{
            padding: isMobile ? '12px 10px' : '16px 14px',
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            justifyContent: 'center',
            borderRight: isMobile ? 'none' : '1px solid var(--dm-line)',
            borderBottom: isMobile ? '1px solid var(--dm-line)' : 'none',
            background: 'var(--dm-bg-0)',
          }}
        >
          <svg
            viewBox={`0 0 ${RING_SIZE} ${RING_SIZE}`}
            style={{ width: RING_SIZE, height: RING_SIZE, overflow: 'visible' }}
          >
            {/* Track rings */}
            <path
              d={trackPath(cx, cy, rOuter)}
              fill='none'
              stroke='rgba(255,255,255,0.06)'
              strokeWidth={stroke}
              strokeLinecap='round'
            />
            <path
              d={trackPath(cx, cy, rMid)}
              fill='none'
              stroke='rgba(255,255,255,0.06)'
              strokeWidth={stroke}
              strokeLinecap='round'
            />
            <path
              d={trackPath(cx, cy, rInner)}
              fill='none'
              stroke='rgba(255,255,255,0.06)'
              strokeWidth={stroke}
              strokeLinecap='round'
            />

            {/* Pressure ring (outer, green) */}
            {pressure > 0 && (
              <path
                d={arcPath(cx, cy, rOuter, pressure / PRESSURE_MAX)}
                fill='none'
                stroke='var(--dm-good)'
                strokeWidth={stroke}
                strokeLinecap='round'
              />
            )}
            <path
              d={tickPath(cx, cy, rOuter, targetPressure / PRESSURE_MAX)}
              stroke='rgba(232,232,232,0.65)'
              strokeWidth='2'
            />

            {/* Flow ring (middle, amber/yellow) */}
            {flowVal > 0 && (
              <path
                d={arcPath(cx, cy, rMid, flowVal / FLOW_MAX)}
                fill='none'
                stroke='var(--dm-warn)'
                strokeWidth={stroke}
                strokeLinecap='round'
              />
            )}
            <path
              d={tickPath(cx, cy, rMid, Math.min(1, targetFlow / FLOW_MAX))}
              stroke='rgba(232,232,232,0.55)'
              strokeWidth='2'
            />

            {/* Temp ring (inner) */}
            <path
              d={arcPath(cx, cy, rInner, temperatureRing.progressFraction)}
              fill='none'
              stroke={tempRingColor}
              strokeWidth={stroke}
              strokeLinecap='round'
            />
            <path
              d={tickPath(cx, cy, rInner, temperatureRing.targetFraction)}
              stroke='rgba(232,232,232,0.55)'
              strokeWidth='2'
            />

            {/* Center: shot timer when active, temp when idle */}
            <text
              x={cx}
              y={cy - 26}
              fontSize='9'
              fill='rgba(232,232,232,0.5)'
              fontFamily='var(--dm-font-mono)'
              textAnchor='middle'
              letterSpacing='3'
            >
              {showProcessTimer ? 'SHOT TIME' : 'TEMPERATURE'}
            </text>
            <text
              x={cx}
              y={cy + 16}
              fontSize={showProcessTimer ? 52 : 40}
              fill={isHeating ? heatingColor : 'var(--dm-fg)'}
              fontFamily='var(--dm-font-display)'
              textAnchor='middle'
              letterSpacing='2'
              fontWeight='700'
              style={isHeating ? { animation: 'dm-pulse 1.2s ease-in-out infinite' } : undefined}
            >
              {showProcessTimer ? fmtTimer(elapsedSecs) : fmt(tempVal, 1)}
            </text>
            <text
              x={cx}
              y={cy + 40}
              fontSize='9'
              fill={showProcessTimer ? 'var(--dm-good)' : isHeating ? heatingColor : 'rgba(232,232,232,0.45)'}
              fontFamily='var(--dm-font-mono)'
              textAnchor='middle'
              letterSpacing='2'
              style={isHeating ? { animation: 'dm-pulse 1.2s ease-in-out infinite' } : undefined}
            >
              {showProcessTimer
                ? currentPhaseLabel
                  ? `${currentPhaseLabel} · ${Math.max(0, targetShotSecs - elapsedSecs)}s LEFT`
                  : `${Math.max(0, targetShotSecs - elapsedSecs)}s LEFT`
                : finished && !isSteamMode
                  ? 'FINISHED'
                  : isSteamMode
                    ? steamStatusLabel
                    : isHeating
                    ? `HEATING · TGT ${fmt(targetTemp)}°`
                    : `TGT ${fmt(targetTemp)}°`}
            </text>
          </svg>

          <div style={{ marginTop: 10, display: 'flex', gap: 18 }}>
            <RingLegend color='var(--dm-good)'   label='PRESSURE' value={`${fmt(pressure)} bar`} />
            <RingLegend color='var(--dm-warn)'   label='FLOW'  value={`${fmt(flowVal)} g/s`} />
            <RingLegend color={tempRingColor} label='TEMP'  value={`${fmt(tempVal)}°`} />
          </div>
        </div>

        {/* Recipe + targets panel */}
        <div
          style={{
            padding: '18px 20px',
            display: 'flex',
            flexDirection: 'column',
            gap: 14,
            minWidth: 0,
            background: 'var(--dm-bg-0)',
          }}
        >
          {isManualMode && isFlushing ? (
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', flex: 1, color: 'var(--dm-fg-dim)', fontFamily: 'var(--dm-font-mono)', fontSize: 11, letterSpacing: '0.18em' }}>
              FLUSHING…
            </div>
          ) : isManualMode ? (
            <ManualConsole
              active={active}
              finished={finished}
              draft={manualDraft}
              selectedBean={s.selectedBean}
              dose={dose}
              currentWeight={currentWeight}
              bluetoothConnected={s.bluetoothConnected}
              scaleName={scaleName}
              pressure={pressure}
              flow={flowVal}
              temperature={tempVal}
              controlLabels={manualControlLabels}
              isBeanDropdownOpen={activeDropdown === 'bean'}
              beanOptions={beanOptions}
              loadingBeans={loadingBeans}
              beanError={beanError}
              primaryAction={primaryAction}
              primaryActionAccent={primaryActionAccent}
              primaryActionLabel={primaryActionLabel}
              onBeanClick={openBeanDropdown}
              onBeanSelect={handleBeanSelect}
              onBeanRetry={loadBeans}
              onBeanDropdownClose={() => setActiveDropdown(null)}
              onDoseCommit={setDose}
              onEditingChange={setIsManualEditing}
              onManualUpdate={sendManualUpdate}
            />
          ) : (
            <>
          {/* Bean + profile — clickable to swap */}
          <div style={{ position: 'relative' }} onClick={e => e.stopPropagation()}>
            <div
              style={{
                fontFamily: 'var(--dm-font-mono)',
                fontSize: 9,
                letterSpacing: '0.18em',
                color: 'var(--dm-fg-dim)',
                marginBottom: 5,
              }}
            >
              TODAY'S RECIPE · TAP TO EDIT
            </div>

            {/* Bean name row */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <button
                type='button'
                onClick={openBeanDropdown}
                style={{
                  background: 'none',
                  border: 'none',
                  padding: 0,
                  cursor: 'pointer',
                  textAlign: 'left',
                  fontFamily: 'var(--dm-font-display)',
                  fontSize: 22,
                  color: 'var(--dm-fg)',
                  fontWeight: 700,
                  lineHeight: 1.05,
                  borderBottom: `1px dashed ${activeDropdown === 'bean' ? 'var(--dm-accent)' : 'rgba(232,232,232,0.2)'}`,
                }}
              >
                {s.selectedBean || 'No bean selected'}
              </button>
              <span
                style={{
                  fontFamily: 'var(--dm-font-mono)',
                  fontSize: 9,
                  color: 'var(--dm-fg-faint)',
                  letterSpacing: '0.12em',
                }}
              >
                ▾
              </span>
            </div>

            {/* Profile row */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginTop: 4 }}>
              <button
                type='button'
                onClick={openProfileDropdown}
                style={{
                  background: 'none',
                  border: 'none',
                  padding: 0,
                  cursor: 'pointer',
                  textAlign: 'left',
                  fontFamily: 'var(--dm-font-mono)',
                  fontSize: 10,
                  color: 'var(--dm-fg-dim)',
                  borderBottom: `1px dashed ${activeDropdown === 'profile' ? 'var(--dm-accent)' : 'transparent'}`,
                }}
              >
                {s.selectedProfile || 'No profile selected'}
              </button>
              <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 8, color: 'var(--dm-fg-faint)' }}>▾</span>
            </div>

            {/* Grinder + grind-setting row (PRO-425) */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 4, flexWrap: 'wrap' }}>
              <button
                type='button'
                onClick={openGrinderDropdown}
                style={{
                  background: 'none',
                  border: 'none',
                  padding: 0,
                  cursor: 'pointer',
                  textAlign: 'left',
                  fontFamily: 'var(--dm-font-mono)',
                  fontSize: 10,
                  color: s.selectedGrinder ? 'var(--dm-fg-dim)' : 'var(--dm-fg-faint)',
                  borderBottom: `1px dashed ${activeDropdown === 'grinder' ? 'var(--dm-accent)' : 'transparent'}`,
                }}
              >
                {s.selectedGrinder || 'No grinder selected'}
              </button>
              <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 8, color: 'var(--dm-fg-faint)' }}>▾</span>
            </div>

            {/* Bean dropdown */}
            {activeDropdown === 'bean' && (
              <SelectDropdown
                label='SELECT BEAN'
                options={beanOptions}
                activeLabel={s.selectedBean}
                onSelect={handleBeanSelect}
                loading={loadingBeans}
                error={beanError}
                onRetry={loadBeans}
                onClose={() => setActiveDropdown(null)}
              />
            )}

            {/* Profile dropdown */}
            {activeDropdown === 'profile' && (
              <SelectDropdown
                label='SELECT PROFILE'
                options={profileOptions}
                activeId={s.selectedProfileId}
                onSelect={handleProfileSelect}
                loading={loadingProfiles}
                error={profileError}
                onRetry={loadProfiles}
                onClose={() => setActiveDropdown(null)}
              />
            )}

            {/* Grinder dropdown (PRO-425) */}
            {activeDropdown === 'grinder' && (
              <SelectDropdown
                label='SELECT GRINDER'
                options={grinderOptions}
                activeLabel={s.selectedGrinder}
                onSelect={handleGrinderSelect}
                loading={loadingGrinders}
                error={grinderError}
                onRetry={loadGrinders}
                onClose={() => setActiveDropdown(null)}
              />
            )}
          </div>

          {/* Grind → Dose → Yield → Scales */}
          <div
            style={getRecipeGridStyle(isMobile)}
          >
            {/* Manual grinder-dial setting (PRO-431, made device-authoritative in
                PRO-603). Edits exactly like DOSE (tap → type → commit); every commit
                fires req:manual-grind:set so the device persists it via NVS and
                broadcasts it as `mg` in evt:status, keeping all browsers in sync.
                localStorage is only an offline seed that primes the UI pre-connection. */}
            <EditableNumBlock
              label='GRIND'
              value={manualGrind}
              unit=''
              step={0.1}
              min={0}
              max={100}
              onCommit={setManualGrind}
              // PRO-503: re-commit the current value on tap-to-open so a
              // tap-away without typing/Enter still records a fresh
              // recordManualGrindSetting event (mobile onBlur is unreliable).
              onTap={() => setManualGrind(manualGrind)}
            />
            <span aria-hidden='true' style={{ display: isMobile ? 'none' : 'inline', fontFamily: 'var(--dm-font-display)', fontSize: 20, color: 'var(--dm-fg-faint)' }}>·</span>
            <EditableNumBlock
              label='DOSE'
              value={dose}
              unit='g'
              step={0.1}
              min={0.1}
              max={200}
              onCommit={setDose}
            />
            <span aria-hidden='true' style={{ display: isMobile ? 'none' : 'inline', fontFamily: 'var(--dm-font-display)', fontSize: 20, color: 'var(--dm-fg-faint)' }}>›</span>
            <EditableNumBlock
              label='YIELD'
              value={targetWeight}
              unit='g'
              hint={`1 : ${(targetWeight / Math.max(dose, 1)).toFixed(2)}`}
              step={0.5}
              min={5}
              max={120}
              onCommit={setYield}
              disabled={!yieldEditable}
              lockedHint={yieldLockReason}
            />
            <span aria-hidden='true' style={{ display: isMobile ? 'none' : 'inline', fontFamily: 'var(--dm-font-display)', fontSize: 20, color: 'var(--dm-fg-faint)' }}>›</span>
            <div>
              <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, letterSpacing: '0.18em', color: 'var(--dm-fg-dim)', marginBottom: 3 }}>
                SCALES
              </div>
              <div style={{ display: 'flex', alignItems: 'baseline', gap: 3 }}>
                <span
                  style={{
                    fontFamily: 'var(--dm-font-display)',
                    fontSize: 28,
                    color: s.bluetoothConnected ? 'var(--dm-good)' : 'var(--dm-fg-faint)',
                    fontWeight: 700,
                    fontVariantNumeric: 'tabular-nums',
                    lineHeight: 1,
                  }}
                >
                  {s.bluetoothConnected ? fmt(currentWeight) : '—'}
                </span>
                <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10, color: 'var(--dm-fg-dim)' }}>g</span>
              </div>
              <div style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, color: 'var(--dm-fg-faint)', marginTop: 2 }}>
                {s.bluetoothConnected ? (scaleName || 'CONNECTED') : 'NOT CONNECTED'}
              </div>
            </div>
          </div>

          {/* Target bars (live shot widgets — hidden in standby, PRO-426) */}
          {!isStandby && (
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 16 }}>
            <TargetBar
              color='var(--dm-good)'
              label='PRESSURE'
              cur={fmt(pressure)}
              tgt={fmt(targetPressure)}
              unit='bar'
              frac={pressure / PRESSURE_MAX}
              tgtFrac={targetPressure / PRESSURE_MAX}
            />
            <TargetBar
              color='var(--dm-warn)'
              label='FLOW'
              cur={fmt(flowVal)}
              tgt={fmt(targetFlow)}
              unit='g/s'
              frac={flowVal / FLOW_MAX}
              tgtFrac={Math.min(1, targetFlow / FLOW_MAX)}
            />
            <TargetBar
              color='var(--dm-accent)'
              label='TEMP'
              cur={fmt(tempVal)}
              tgt={fmt(targetTemp)}
              unit='°C'
              frac={temperatureRing.progressFraction}
              tgtFrac={temperatureRing.targetFraction}
            />
          </div>
          )}

          {/* Bottom row + action button.
              Standby (PRO-426): grinder + grind setting + selected-profile
              mini-curve. All other modes: the live yield fill bar. */}
          <div
            style={{
              display: 'grid',
              gridTemplateColumns: '1fr 160px',
              gap: 14,
              alignItems: 'stretch',
              marginTop: 'auto',
            }}
          >
            {isStandby ? (
              <StandbyBlock
                profileName={s.selectedProfile}
                curve={standbyCurve}
                profileError={profileDataError.profile}
                onRetryProfile={retryProfileData.profile}
              />
            ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
              <div
                style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'baseline',
                }}
              >
                <span
                  style={{
                    fontFamily: 'var(--dm-font-mono)',
                    fontSize: 9,
                    letterSpacing: '0.18em',
                    color: 'var(--dm-fg-dim)',
                  }}
                >
                  YIELD
                </span>
                <span style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 10 }}>
                  <span
                    style={{
                      fontFamily: 'var(--dm-font-display)',
                      fontSize: 20,
                      color: 'var(--dm-fg)',
                      fontWeight: 700,
                    }}
                  >
                    {fmt(currentWeight)}
                  </span>
                  <span style={{ color: 'var(--dm-fg-dim)', marginLeft: 5 }}>
                    / {targetWeight.toFixed(1)}g · 1:
                    {(currentWeight / Math.max(dose, 1)).toFixed(2)}
                  </span>
                </span>
              </div>
              <div style={{ display: 'flex', gap: 2, height: 10 }}>
                {YIELD_SEGMENTS.map((i) => (
                  <span
                    key={i}
                    style={{
                      flex: 1,
                      borderRadius: 1,
                      background:
                        i / 40 < currentWeight / Math.max(targetWeight, 1)
                          ? 'var(--dm-accent)'
                          : 'var(--dm-bg-3)',
                    }}
                  />
                ))}
              </div>
            </div>
            )}

            <button
              type='button'
              onClick={primaryAction}
              style={getPrimaryActionButtonStyle({ active, finished, accent: primaryActionAccent })}
            >
              {primaryActionLabel}
            </button>
            {primaryActionLabel === 'WAKE' && (
              <span role='status' aria-live='polite' style={{ fontFamily: 'var(--dm-font-mono)', fontSize: 9, color: 'var(--dm-fg-dim)' }}>
                Wake, then tap again to start.
              </span>
            )}
          </div>
            </>
          )}
        </div>
      </div>

      {/* Bottom: full-width live extraction graph */}
      <div
        style={{
          padding: '12px 16px',
          display: 'flex',
          flexDirection: 'column',
          gap: 8,
          background: 'var(--dm-bg-0)',
          flex: 1,
          minHeight: isMobile ? 100 : 140,
        }}
      >
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
          }}
        >
          <span
            style={{
              fontFamily: 'var(--dm-font-mono)',
              fontSize: 9,
              letterSpacing: '0.18em',
              color: 'var(--dm-fg-dim)',
            }}
          >
            LIVE EXTRACTION
          </span>
          <div style={graphLegendRowStyle}>
            <GraphLegend color='var(--dm-accent)' label='TEMP'     value={`${fmt(tempVal)}°C`} />
            <GraphLegend color='var(--dm-good)'   label='PRESSURE' value={`${fmt(pressure)} bar`} />
            <GraphLegend color='var(--dm-warn)'   label='FLOW'     value={`${fmt(flowVal)} g/s`} />
            <GraphLegend color='rgba(232,232,232,0.42)' label='TARGET' dashed />
          </div>
        </div>

        <div
          className='dm-scan'
          style={{
            flex: 1,
            position: 'relative',
            border: '1px solid var(--dm-line)',
            borderRadius: 8,
            background: 'var(--dm-bg-0)',
            overflow: 'hidden',
          }}
        >
          <svg
            viewBox={`0 0 ${GW} ${GH}`}
            preserveAspectRatio='none'
            style={{ width: '100%', height: '100%', display: 'block' }}
          >
            {/* Left axis: pressure/flow grid lines (0–12 bar) */}
            {[0, 3, 6, 9, 12].map(p => {
              const y = pToY(p).toFixed(1);
              return (
                <g key={p}>
                  <line
                    x1={GP_L}
                    x2={GW - GP_R}
                    y1={y}
                    y2={y}
                    stroke='rgba(255,255,255,0.05)'
                    strokeDasharray='2 4'
                  />
                  <text
                    x={GP_L - 5}
                    y={parseFloat(y) + 3}
                    fontSize='9'
                    fill='rgba(124,184,118,0.55)'
                    fontFamily='var(--dm-font-mono)'
                    textAnchor='end'
                  >
                    {p}
                  </text>
                </g>
              );
            })}

            {/* Right axis: temperature labels (0–110 °C) */}
            {[0, 30, 60, 90, 110].map(t => {
              const y = tempToY(t).toFixed(1);
              return (
                <text
                  key={t}
                  x={GW - GP_R + 5}
                  y={parseFloat(y) + 3}
                  fontSize='9'
                  style={{ fill: 'color-mix(in srgb, var(--dm-accent) 55%, transparent)', fontFamily: 'var(--dm-font-mono)' }}
                  textAnchor='start'
                >
                  {t}°
                </text>
              );
            })}

            {graphPaths ? (
              <>
                {/* Target pressure (dashed, faint) */}
                {graphPaths.tpd && (
                  <path
                    d={graphPaths.tpd}
                    fill='none'
                    stroke='rgba(232,232,232,0.35)'
                    strokeWidth='1.5'
                    strokeDasharray='3 3'
                  />
                )}
                {/* Flow line (yellow/amber) */}
                {graphPaths.fd && (
                  <path
                    d={graphPaths.fd}
                    fill='none'
                    stroke='var(--dm-warn)'
                    strokeWidth='1.5'
                  />
                )}
                {/* Pressure line (green) */}
                {graphPaths.pd && (
                  <path
                    d={graphPaths.pd}
                    fill='none'
                    stroke='var(--dm-good)'
                    strokeWidth='2'
                  />
                )}
                {/* Temperature line (red, hero) */}
                {graphPaths.tempd && (
                  <path
                    d={graphPaths.tempd}
                    fill='none'
                    stroke='var(--dm-accent)'
                    strokeWidth='2.5'
                  />
                )}
                {/* Playhead (only during active brew) */}
                {active && graphPaths.curX && (
                  <>
                    <line
                      x1={graphPaths.curX}
                      x2={graphPaths.curX}
                      y1={GP_T}
                      y2={GP_T + G_INNER_H}
                      stroke='rgba(232,232,232,0.3)'
                      strokeWidth='1'
                    />
                    <circle
                      cx={graphPaths.curX}
                      cy={graphPaths.curPY}
                      r='4'
                      fill='var(--dm-good)'
                      stroke='#000'
                      strokeWidth='1.5'
                    />
                  </>
                )}
              </>
            ) : (
              <text
                x={GW / 2}
                y={GH / 2}
                fontSize='11'
                fill='rgba(232,232,232,0.25)'
                fontFamily='var(--dm-font-mono)'
                textAnchor='middle'
                letterSpacing='3'
              >
                NO DATA
              </text>
            )}
          </svg>
        </div>
      </div>
    </div>
  );
}
