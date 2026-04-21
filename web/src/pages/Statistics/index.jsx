import { useMemo, useState } from 'preact/hooks';
import { useRoute } from 'preact-iso';
import { StatisticsView } from './components/StatisticsView';
import { parseStatisticsProfileRouteParams } from './utils/statisticsRoute';

export function StatisticsPage() {
  const { params } = useRoute();
  const [sessionInitialContext] = useState(() => {
    try {
      const raw = sessionStorage.getItem('statsInitialContext');
      if (raw) {
        sessionStorage.removeItem('statsInitialContext');
        return JSON.parse(raw);
      }
    } catch { /* ignore */ }
    return null;
  });
  const routeInitialContext = useMemo(() => parseStatisticsProfileRouteParams(params), [params]);
  const initialContext = routeInitialContext || sessionInitialContext || { source: 'gaggimate' };

  return (
    <div class="pb-20 space-y-6">
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-2xl font-semibold text-[--text-primary]">Statistics</h1>
          <p class="text-sm text-[--text-secondary] mt-1">Visualize shot trends and performance metrics</p>
        </div>
      </div>
      <StatisticsView initialContext={initialContext} />
    </div>
  );
}