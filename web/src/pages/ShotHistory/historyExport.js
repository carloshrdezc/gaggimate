import { downloadBlob } from '../../utils/download.js';

const CSV_COLUMNS = [
  'id',
  'timestamp',
  'profile',
  'duration_s',
  'dose_in_g',
  'dose_out_g',
  'ratio',
  'peak_pressure_bar',
  'avg_pressure_bar',
  'peak_flow_ml_s',
  'avg_temp_c',
  'notes',
];

function escapeCsv(value) {
  const str = String(value ?? '');
  if (/[",\n]/.test(str)) {
    return `"${str.replaceAll('"', '""')}"`;
  }
  return str;
}

function sampleValues(samples, field) {
  return (samples || [])
    .map(sample => sample?.[field])
    .filter(value => typeof value === 'number' && !Number.isNaN(value));
}

function peak(values) {
  return values.length ? Math.max(...values).toFixed(2) : '';
}

function avg(values) {
  return values.length ? (values.reduce((sum, v) => sum + v, 0) / values.length).toFixed(2) : '';
}

function shotToRow(shot) {
  const pressures = sampleValues(shot.samples, 'cp');
  const flows = sampleValues(shot.samples, 'fl');
  const temps = sampleValues(shot.samples, 'ct');

  return [
    shot.id,
    new Date(shot.timestamp * 1000).toISOString(),
    shot.profile,
    shot.duration,
    shot.notes?.doseIn || '',
    shot.notes?.doseOut || '',
    shot.notes?.ratio || '',
    peak(pressures),
    avg(pressures),
    peak(flows),
    avg(temps),
    shot.notes?.notes || '',
  ];
}

export function exportShotsAsCsv(shots, filename) {
  const rows = [CSV_COLUMNS, ...(shots || []).map(shotToRow)];
  const csv = rows.map(row => row.map(escapeCsv).join(',')).join('\n');
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' });
  downloadBlob(blob, filename);
}
