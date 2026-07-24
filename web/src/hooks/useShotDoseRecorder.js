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
  // Per-field save tracking for the current shot. Shape: { shotId, dose, grind }
  // where `dose`/`grind` are booleans marking whether that field's write has
  // already succeeded for this shot. Kept per-field (rather than a single
  // "allSucceeded" boolean) so a retry after a partial failure only re-attempts
  // the field(s) that actually failed, never redundantly resending a sibling
  // field that already persisted.
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

    // Skip if not active, or if an attempt for this shot is still in flight.
    if (!isActive || inFlightForShotRef.current === shotId) {
      return;
    }

    // Start fresh per-field tracking whenever a new shot ID appears.
    if (savedForShotRef.current?.shotId !== shotId) {
      savedForShotRef.current = { shotId, dose: false, grind: false };
    }
    const saved = savedForShotRef.current;

    const hasDose = Number.isFinite(dose) && dose > 0;
    const beanType = status.value.selectedBean || '';
    const hasGrind = Number.isFinite(manualGrind) && manualGrind > 0;

    // dose/bean are attached unconditionally (read-merge-write); grind is filled
    // only if absent so it never clobbers an in-shot commit or a manual edit.
    const doseNotes = {};
    if (hasDose) doseNotes.doseIn = dose;
    if (beanType) doseNotes.beanType = beanType;
    const hasDoseNotes = hasDose || !!beanType;

    // Only re-attempt fields that are still outstanding: applicable to this shot
    // and not already persisted on a prior attempt.
    const needDose = hasDoseNotes && !saved.dose;
    const needGrind = hasGrind && !saved.grind;

    if (!needDose && !needGrind) return;

    inFlightForShotRef.current = shotId;
    notesService.setApiService(api);
    (async () => {
      let doseAttached = false;

      if (needDose) {
        try {
          // The device applies the bean-quantity delta while saving notes, using
          // the already-persisted notes as the idempotency source of truth.
          await notesService.saveNotes(shotId, 'gaggimate', doseNotes);
          saved.dose = true;
          doseAttached = true;
        } catch (err) {
          console.error('Failed to attach dose/bean notes to shot:', err);
        }
      }

      if (needGrind) {
        try {
          // fill-only-if-absent: never overwrite an existing grindSetting.
          await notesService.saveNotesIfMissing(shotId, 'gaggimate', {
            grindSetting: String(manualGrind),
          });
          saved.grind = true;
        } catch (err) {
          console.error('Failed to attach manual grind setting to shot:', err);
        }
      }

      // Per-field flags are updated above; a field left false will be retried on
      // the next status tick, while a field already true is skipped — so a retry
      // only resends the field(s) that actually failed.
      if (inFlightForShotRef.current === shotId) inFlightForShotRef.current = null;

      if (mountedRef.current && doseAttached) {
        onDoseAttached?.(hasDose ? dose : null);
      }
    })();
  }, [status.value.process, api, dose, manualGrind]);
}
