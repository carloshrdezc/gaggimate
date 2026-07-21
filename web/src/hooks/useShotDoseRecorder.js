import { useEffect, useRef } from 'preact/hooks';
import { computed } from '@preact/signals';
import { machine } from '../services/ApiService.js';
import { notesService } from '../pages/ShotAnalyzer/services/NotesService';

// dose: numeric grams value from the caller's state (not localStorage, so it persists across shots)
// manualGrind: numeric manual grinder-dial value from the caller's state (persists across shots).
//   Only attached when > 0, and only if the shot's notes don't already carry a
//   grindSetting (fill-only-if-absent) so an in-shot commit or a manual Shot Notes
//   edit is never clobbered.
export function useShotDoseRecorder(api, dose, manualGrind, onDoseAttached) {
  const status = computed(() => machine.value.status);
  const mountedRef = useRef(true);
  // Set only once the shot-start attach actually succeeds. Left null on failure
  // so a later status tick / re-render retries instead of permanently skipping.
  const savedForShotRef = useRef(null);
  // Guards against concurrent attempts for the same shot when the effect re-runs
  // (dose/grind change, new status frame) while a prior attempt is still pending.
  const inFlightForShotRef = useRef(null);

  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  useEffect(() => {
    const processInfo = status.value.process;
    const shotId = processInfo?.id;
    const isActive = !!processInfo?.a;

    // Reset when no shot is running
    if (!shotId) {
      savedForShotRef.current = null;
      return;
    }

    // Attach once per shot, as soon as the shot ID appears while active. Skip if
    // already saved, or if an attempt for this shot is still in flight.
    if (!isActive || savedForShotRef.current === shotId || inFlightForShotRef.current === shotId) {
      return;
    }

    const hasDose = Number.isFinite(dose) && dose > 0;
    const beanType = status.value.selectedBean || '';
    const hasGrind = Number.isFinite(manualGrind) && manualGrind > 0;

    // dose/bean are attached unconditionally (read-merge-write); grind is filled
    // only if absent so it never clobbers an in-shot commit or a manual edit.
    const doseNotes = {};
    if (hasDose) doseNotes.doseIn = dose;
    if (beanType) doseNotes.beanType = beanType;
    const hasDoseNotes = hasDose || !!beanType;

    if (!hasDoseNotes && !hasGrind) return;

    inFlightForShotRef.current = shotId;
    notesService.setApiService(api);
    (async () => {
      let allSucceeded = true;
      let doseAttached = false;

      if (hasDoseNotes) {
        try {
          // The device applies the bean-quantity delta while saving notes, using
          // the already-persisted notes as the idempotency source of truth.
          await notesService.saveNotes(shotId, 'gaggimate', doseNotes);
          doseAttached = true;
        } catch (err) {
          allSucceeded = false;
          console.error('Failed to attach dose/bean notes to shot:', err);
        }
      }

      if (hasGrind) {
        try {
          // fill-only-if-absent: never overwrite an existing grindSetting.
          await notesService.saveNotesIfMissing(shotId, 'gaggimate', {
            grindSetting: String(manualGrind),
          });
        } catch (err) {
          allSucceeded = false;
          console.error('Failed to attach manual grind setting to shot:', err);
        }
      }

      // Only mark the shot as saved once every attempted write succeeded; on any
      // failure leave the ref null so the next status tick retries this shot ID.
      if (allSucceeded) savedForShotRef.current = shotId;
      if (inFlightForShotRef.current === shotId) inFlightForShotRef.current = null;

      if (mountedRef.current && doseAttached) {
        onDoseAttached?.(hasDose ? dose : null);
      }
    })();
  }, [status.value.process, api, dose, manualGrind]);
}
