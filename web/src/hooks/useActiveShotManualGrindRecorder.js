import { useCallback, useRef } from 'preact/hooks';
import { machine } from '../services/ApiService.js';
import { notesService } from '../pages/ShotAnalyzer/services/NotesService';

const inFlightByShot = new WeakMap();

function activeShotIdentity(status) {
  const process = status?.process;
  const shotId = process?.id;
  if (!process?.a || !shotId) return null;

  // `rid` is reserved for a future status-frame process token. Current firmware
  // identifies the recording shot by id, so do not manufacture a transport token.
  return { shotId: String(shotId), requestId: process.rid ?? process.requestId ?? null };
}

function isSameActiveShot(getStatus, expected) {
  const current = activeShotIdentity(getStatus());
  return current?.shotId === expected.shotId && current.requestId === expected.requestId;
}

/**
 * Fill an active shot's manual grind setting exactly once.
 *
 * The active process identity is checked before the request. A status response
 * for a shot that ended or changed can therefore never be sent for its successor.
 * Requests for one active-shot identity are deduplicated; callers may invalidate
 * an older edit with isRequestCurrent. The device owns the final
 * active-shot/missing-field check in one locked request.
 */
export async function persistManualGrindForActiveShot({
  api,
  grindSetting,
  getStatus,
  notesService: service = notesService,
  isRequestCurrent = () => true,
}) {
  const value = String(grindSetting ?? '').trim();
  const activeShot = activeShotIdentity(getStatus());
  if (!value || !activeShot || !isRequestCurrent()) return;

  let inFlight = inFlightByShot.get(api);
  if (!inFlight) {
    inFlight = new Map();
    inFlightByShot.set(api, inFlight);
  }

  const key = `${activeShot.shotId}:${activeShot.requestId ?? ''}`;
  if (inFlight.has(key)) return inFlight.get(key);

  const operation = (async () => {
    service.setApiService(api);
    if (!isSameActiveShot(getStatus, activeShot) || !isRequestCurrent()) return;

    await service.saveNotesIfMissing(
      activeShot.shotId,
      'gaggimate',
      { grindSetting: value },
      () => isSameActiveShot(getStatus, activeShot) && isRequestCurrent(),
    );
  })();

  inFlight.set(key, operation);
  try {
    await operation;
  } finally {
    if (inFlight.get(key) === operation) inFlight.delete(key);
  }
}

export function useActiveShotManualGrindRecorder(api) {
  const latestRequestRef = useRef(0);

  return useCallback(
    grindSetting => {
      const requestId = ++latestRequestRef.current;
      persistManualGrindForActiveShot({
        api,
        grindSetting,
        getStatus: () => machine.value.status,
        isRequestCurrent: () => latestRequestRef.current === requestId,
      }).catch(error => {
        console.error('Failed to attach manual grind setting to active shot:', error);
      });
    },
    [api],
  );
}
