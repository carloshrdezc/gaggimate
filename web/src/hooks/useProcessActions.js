import { useEffect, useMemo } from 'preact/hooks';

// PRO-655 B-P2-2: the display replies to req:process:activate / req:grind:activate
// ONLY when it refuses the start (controller version mismatch / not connected).
export const PROCESS_REFUSAL_TYPES = ['res:process:activate', 'res:grind:activate'];

/** Returns the user-facing refusal text for a refusal reply, or null. */
export function processRefusalMessage(message) {
  if (!message || !PROCESS_REFUSAL_TYPES.includes(message.tp)) return null;
  if (message.success !== false) return null;
  return message.error || 'Controller not ready';
}

const alertNotify = msg => window.alert(msg);

/** Subscribes to process-start refusals and shows them to the user. */
export function useProcessRefusalNotice(api, notify = alertNotify) {
  useEffect(() => {
    if (!api || typeof api.on !== 'function') return undefined;
    const ids = PROCESS_REFUSAL_TYPES.map(tp => [
      tp,
      api.on(tp, message => {
        const text = processRefusalMessage(message);
        if (text) notify(`Cannot start: ${text}`);
      }),
    ]);
    return () => ids.forEach(([tp, id]) => api.off(tp, id));
  }, [api, notify]);
}

/**
 * Custom hook to create memoized action handlers for process control
 *
 * @param {Object} api - ApiService instance
 * @param {boolean} grind - Whether in grind mode
 * @param {Function} setIsFlushing - State setter for flush status
 * @param {Object} lastProcessTypeRef - Optional ref to track 'brew', 'steam', or 'flush'
 * @param {string} processKind - Process kind to record when activating
 * @returns {Object} Action handler functions
 */
export function useProcessActions(
  api,
  grind,
  setIsFlushing,
  lastProcessTypeRef,
  processKind = grind ? 'grind' : 'brew',
) {
  useProcessRefusalNotice(api);
  return useMemo(
    () => ({
      changeTarget: target => {
        const tp = grind ? 'req:change-grind-target' : 'req:change-brew-target';
        api.send({ tp, target });
      },
      activate: () => {
        if (lastProcessTypeRef) lastProcessTypeRef.current = processKind;
        api.send({ tp: grind ? 'req:grind:activate' : 'req:process:activate' });
      },
      deactivate: () => {
        api.send({ tp: grind ? 'req:grind:deactivate' : 'req:process:deactivate' });
      },
      clear: () => {
        api.send({ tp: 'req:process:clear' });
      },
      raiseTemp: () => {
        api.send({ tp: 'req:raise-temp' });
      },
      lowerTemp: () => {
        api.send({ tp: 'req:lower-temp' });
      },
      raiseTarget: () => {
        api.send({ tp: grind ? 'req:raise-grind-target' : 'req:raise-brew-target' });
      },
      lowerTarget: () => {
        api.send({ tp: grind ? 'req:lower-grind-target' : 'req:lower-brew-target' });
      },
      startFlush: () => {
        if (lastProcessTypeRef) lastProcessTypeRef.current = 'flush';
        setIsFlushing(true);
        api.request({ tp: 'req:flush:start' }).catch(error => {
          console.error('Flush request failed:', error);
          setIsFlushing(false);
          if (error?.message && !/timed out/.test(error.message)) {
            window.alert(`Cannot start: ${error.message}`);
          }
        });
      },
    }),
    [api, grind, setIsFlushing, lastProcessTypeRef, processKind],
  );
}

// Made with Bob
