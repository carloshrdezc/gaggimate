import { useEffect, useRef } from 'preact/hooks';
import Chart from 'chart.js/auto';

const SHOT_COLORS = ['#3b82f6', '#f97316', '#22c55e', '#a855f7']; // blue, orange, green, purple

export function ComparisonChart({ shots, comparisonData }) {
  const canvasRef = useRef(null);
  const chartRef = useRef(null);

  useEffect(() => {
    if (!canvasRef.current || shots.length < 2) return;
    if (chartRef.current) {
      chartRef.current.destroy();
      chartRef.current = null;
    }

    const datasets = [];
    shots.forEach((shot, i) => {
      const data = comparisonData[shot.id];
      if (!data?.samples) return;
      const color = SHOT_COLORS[i % SHOT_COLORS.length];
      const label =
        new Date(shot.timestamp * 1000).toLocaleDateString() +
        (shot.profile ? ' · ' + shot.profile : '');
      const samples = data.samples;
      const pressureData = samples.map(s => ({ x: s.t, y: s.cp ?? s.tp ?? null }));
      const flowData = samples.map(s => ({ x: s.t, y: s.fl ?? null }));

      datasets.push({
        label: label + ' (pressure)',
        data: pressureData,
        borderColor: color,
        backgroundColor: 'transparent',
        borderWidth: 2,
        tension: 0.3,
        pointRadius: 0,
        yAxisID: 'yPressure',
      });
      datasets.push({
        label: label + ' (flow)',
        data: flowData,
        borderColor: color,
        backgroundColor: 'transparent',
        borderWidth: 1.5,
        borderDash: [4, 2],
        tension: 0.3,
        pointRadius: 0,
        yAxisID: 'yFlow',
      });
    });

    // Don't render an empty chart if no shot data has loaded yet
    if (datasets.length === 0) return;

    chartRef.current = new Chart(canvasRef.current, {
      type: 'line',
      data: { datasets },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        scales: {
          x: { type: 'linear', title: { display: true, text: 'Time (s)' } },
          yPressure: {
            type: 'linear',
            position: 'left',
            title: { display: true, text: 'Pressure (bar)' },
            min: 0,
            suggestedMax: 12,
          },
          yFlow: {
            type: 'linear',
            position: 'right',
            title: { display: true, text: 'Flow (ml/s)' },
            min: 0,
            suggestedMax: 8,
            grid: { drawOnChartArea: false },
          },
        },
        plugins: {
          legend: { position: 'top', labels: { font: { size: 11 } } },
        },
      },
    });

    return () => {
      if (chartRef.current) {
        chartRef.current.destroy();
        chartRef.current = null;
      }
    };
  }, [shots, comparisonData]);

  // Determine how many shots are still loading vs failed
  const loadedCount = shots.filter(s => comparisonData[s.id]?.samples).length;
  const isLoading = shots.length >= 2 && loadedCount < shots.length;

  return (
    <div style={{ height: 320, position: 'relative' }}>
      {isLoading && loadedCount === 0 && (
        <div
          style={{
            position: 'absolute',
            inset: 0,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
          }}
          className='font-nd-mono text-[12px] text-[var(--text-secondary,#999)]'
        >
          Loading comparison data…
        </div>
      )}
      <canvas ref={canvasRef} />
    </div>
  );
}
