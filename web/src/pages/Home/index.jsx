import { useState, useEffect, useContext, useCallback } from 'preact/hooks';
import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { TopBar } from '../../components/TopBar.jsx';
import { StatusStrip } from '../../components/StatusStrip.jsx';
import { OverviewChart } from '../../components/OverviewChart.jsx';
import { ProcessDisplay } from '../../components/ProcessDisplay.jsx';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlay, faPause, faStop, faTint } from '@fortawesome/free-solid-svg-icons';
import { useProcessActions } from '../../hooks/useProcessActions.js';
import { useProfileData } from '../../hooks/useProfileData.js';

const MODE_LABELS = ['Standby', 'Brew', 'Steam', 'Water', 'Grind'];

export function Home() {
  const api = useContext(ApiServiceContext);
  const status = machine.value.status;
  const mode = status.mode;
  const brew = mode === 1;
  const grind = mode === 4;
  const processInfo = status.process;
  const active = !!processInfo?.a;
  const finished = !!processInfo?.e && !active;

  const [isFlushing, setIsFlushing] = useState(false);
  const { profiles, profileData, loading } = useProfileData(api, brew, status.selectedProfileId);

  const actions = useProcessActions(api, grind, setIsFlushing);

  const handleStart = useCallback(() => {
    actions.activate();
  }, [actions]);

  const handlePause = useCallback(() => {
    actions.deactivate();
    if (isFlushing) actions.clear();
  }, [actions, isFlushing]);

  const handleStop = useCallback(() => {
    actions.clear();
  }, [actions]);

  const handleFlush = useCallback(() => {
    actions.startFlush();
  }, [actions]);

  // Get button states
  const isIdle = !active && !finished;
  const canFlush = brew && isIdle;

  return (
    <div class="min-h-screen bg-[--bg-base]">
      {/* Fixed Top Bar */}
      <TopBar />

      {/* Main content - offset for fixed topbar */}
      <main class="pt-12">
        <div class="max-w-[1400px] mx-auto px-4 py-6 space-y-6">

          {/* Status Strip */}
          <StatusStrip />

          {/* Process Display + Chart - Hero Area */}
          <div class="space-y-4">
            {/* Profile name + phase indicator */}
            {status.selectedProfile && (
              <div class="flex items-center justify-between">
                <div class="flex items-center gap-3">
                  <h2 class="text-lg font-semibold text-[--text-primary]">{status.selectedProfile}</h2>
                  {processInfo?.l && (
                    <span class="text-sm text-[--text-secondary]">· {processInfo.l}</span>
                  )}
                </div>
                {processInfo?.pt && processInfo?.pp && (
                  <span class="text-sm text-[--text-muted]">
                    Phase {Math.round((processInfo.pp / processInfo.pt) * 100)}%
                  </span>
                )}
              </div>
            )}

            {/* Process Chart - live updating */}
            <div class="h-[300px] bg-[--bg-elevated] rounded-lg border border-[--border] p-4">
              <OverviewChart />
            </div>

            {/* Process Progress Bar */}
            {active && processInfo?.pt && (
              <div class="space-y-2">
                <div class="w-full h-2 bg-[--bg-base] rounded-full overflow-hidden">
                  <div
                    class="h-full bg-[--accent] transition-all duration-300 ease-out"
                    style={{ width: `${(processInfo.pp / processInfo.pt) * 100}%` }}
                  />
                </div>
                <div class="flex justify-between text-xs text-[--text-muted]">
                  <span>{processInfo.l || 'Progress'}</span>
                  <span>{Math.round((processInfo.pp / processInfo.pt) * 100)}%</span>
                </div>
              </div>
            )}
          </div>

          {/* Quick Actions */}
          <div class="flex items-center gap-3">
            {/* Start/Pause Button */}
            <button
              onClick={active ? handlePause : handleStart}
              class={`inline-flex items-center gap-2 px-5 py-2.5 rounded-lg font-medium transition-all ${
                active
                  ? 'bg-[--warning] text-[--bg-base] hover:bg-amber-500'
                  : 'bg-[--accent] text-[--bg-base] hover:bg-orange-500'
              }`}
            >
              <FontAwesomeIcon icon={active ? faPause : faPlay} class="text-lg" />
              <span>{active ? 'Pause' : 'Start'}</span>
            </button>

            {/* Stop Button */}
            {(active || finished) && (
              <button
                onClick={handleStop}
                class="inline-flex items-center gap-2 px-5 py-2.5 rounded-lg font-medium bg-[--bg-elevated] border border-[--border] text-[--text-primary] hover:border-[--border-active] transition-all"
              >
                <FontAwesomeIcon icon={faStop} class="text-lg" />
                <span>Stop</span>
              </button>
            )}

            {/* Flush Button */}
            {canFlush && (
              <button
                onClick={handleFlush}
                class="inline-flex items-center gap-2 px-5 py-2.5 rounded-lg font-medium bg-[--bg-elevated] border border-[--border] text-[--text-secondary] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
              >
                <FontAwesomeIcon icon={faTint} class="text-lg" />
                <span>Flush</span>
              </button>
            )}
          </div>

          {/* Profile Selector */}
          <div class="flex items-center gap-4">
            <div class="flex-1 max-w-xs">
              <label class="block text-xs text-[--text-muted] uppercase tracking-wider mb-2">
                Active Profile
              </label>
              <select
                value={status.selectedProfileId || ''}
                onChange={(e) => {
                  api.request({ tp: 'req:profiles:select', id: e.target.value }).catch(console.error);
                }}
                class="w-full px-4 py-2.5 bg-[--bg-elevated] border border-[--border] rounded-lg text-[--text-primary] focus:border-[--accent] focus:outline-none transition-all appearance-none cursor-pointer"
                style={{ backgroundImage: `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' fill='none' viewBox='0 0 24 24' stroke='%23ffffff30'%3E%3Cpath stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M19 9l-7 7-7-7'%3E%3C/path%3E%3C/svg%3E")`, backgroundRepeat: 'no-repeat', backgroundPosition: 'right 12px center', backgroundSize: '16px' }}
              >
                <option value="">Select Profile</option>
                {profiles.map(profile => (
                  <option key={profile.id} value={profile.id}>
                    {profile.label || profile.id}
                  </option>
                ))}
              </select>
            </div>

            {/* Quick stats */}
            <div class="flex items-center gap-6 text-sm">
              <div>
                <span class="text-[--text-muted] text-xs uppercase tracking-wider">Target</span>
                <div class="text-[--text-primary] font-data">{status.targetTemperature}°C</div>
              </div>
              {status.grindTarget !== undefined && (
                <div>
                  <span class="text-[--text-muted] text-xs uppercase tracking-wider">Grind</span>
                  <div class="text-[--text-primary] font-data">
                    {status.grindTarget === 1 ? `${status.grindTargetVolume}g` : `${Math.round(status.grindTargetDuration / 1000)}s`}
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Recent Shots - real data from API when available */}
          <div class="space-y-3">
            <h3 class="text-sm font-semibold text-[--text-secondary] uppercase tracking-wider">
              Recent Shots
            </h3>

            {/* TODO: Integrate with shot history API */}
            <div class="text-sm text-[--text-muted] py-4">
              No recent shots available.
            </div>
          </div>

        </div>
      </main>
    </div>
  );
}

// ShotRow component kept for future use
// function ShotRow({ type, weightIn, weightOut, time, score }) {
//   return (
//     <div class="flex items-center justify-between px-4 py-3 bg-[--bg-elevated] rounded-lg border border-[--border] hover:border-[--border-active] transition-all cursor-pointer">
//       <div class="flex items-center gap-4">
//         <span class="text-sm font-medium text-[--accent]">{type}</span>
//         <span class="text-[--text-muted]">·</span>
//         <span class="text-sm text-[--text-secondary] font-data">{weightIn}g → {weightOut}g</span>
//         <span class="text-[--text-muted]">·</span>
//         <span class="text-sm text-[--text-secondary] font-data">{time}</span>
//       </div>
//       <div class="flex items-center gap-3">
//         <span class="text-sm text-[--text-muted]">score:</span>
//         <span class="text-sm font-semibold text-[--text-primary]">{score}</span>
//       </div>
//     </div>
//   );
// }