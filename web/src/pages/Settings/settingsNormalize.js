// Shared normalization for /api/settings payloads.
//
// The settings page holds the auto-wakeup schedule in two places: the `formData`
// object (raw device shape, what gets exported) and a separate
// `autowakeupSchedules` array of `{ time, days[] }` that drives the editor UI and
// the POST body. Both the device-fetch effect and settings import must derive that
// array (and split the combined `pid`/`kf` field) the same way, or import silently
// keeps the stale schedule. Keep this the single source of truth (CAR/PRO-252).

export const DEFAULT_AUTOWAKEUP_SCHEDULE = () => ({
  time: '07:00',
  days: [true, true, true, true, true, true, true],
});

// Parse the device's `autowakeupSchedules` string (";"-separated entries, each
// `time|<7 chars of 0/1>`) into the editor's `[{ time, days: bool[7] }]` array.
// Falls back to a single all-days 07:00 schedule when absent/empty/malformed.
export function parseAutoWakeupSchedules(raw) {
  const schedules = [];
  if (typeof raw === 'string' && raw.trim()) {
    const scheduleStrings = raw.split(';');
    for (const scheduleStr of scheduleStrings) {
      const [time, daysStr] = scheduleStr.split('|');
      if (time && daysStr && daysStr.length === 7) {
        const days = daysStr.split('').map(d => d === '1');
        schedules.push({ time, days });
      }
    }
  }
  if (schedules.length === 0) {
    schedules.push(DEFAULT_AUTOWAKEUP_SCHEDULE());
  }
  return schedules;
}

// Normalize a raw settings object (from the device GET or an imported file) into
// the shape `formData` expects: split the combined `pid` CSV into `pid` (first 3
// parts) + `kf` (4th part), and derive `standbyDisplayEnabled`/`dashboardLayout`
// defaults. `dashboardLayouts.ORDER_FIRST` is passed in to avoid an import cycle.
//
// Returns `{ formData, autowakeupSchedules }` so callers can apply both states.
export function normalizeSettings(raw, { defaultDashboardLayout } = {}) {
  const settings = { ...raw };

  settings.standbyDisplayEnabled =
    raw.standbyDisplayEnabled !== undefined ? raw.standbyDisplayEnabled : raw.standbyBrightness > 0;
  settings.dashboardLayout = raw.dashboardLayout || defaultDashboardLayout;

  if (raw.pid) {
    const pidParts = String(raw.pid).split(',');
    if (pidParts.length >= 4) {
      // Device GET shape: combined "kp,ki,kd,kf" CSV. Split into pid + kf.
      settings.pid = pidParts.slice(0, 3).join(',');
      settings.kf = pidParts[3];
    } else if (raw.kf === undefined) {
      // Already-split shape with no kf (legacy/partial). Default kf.
      settings.kf = '0.000';
    }
    // Already-split shape with an explicit kf (e.g. an exported settings file):
    // keep both raw.pid and raw.kf as-is so PID round-trips through export/import.
  }

  const autowakeupSchedules = parseAutoWakeupSchedules(raw.autowakeupSchedules);

  return { formData: settings, autowakeupSchedules };
}
