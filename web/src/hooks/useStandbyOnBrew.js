import { useCallback, useContext } from 'preact/hooks';
import { computed, signal } from '@preact/signals';
import { ApiServiceContext, machine } from '../services/ApiService.js';

const STANDBY_ON_BREW_KEY = 'gaggimate-standby-on-brew';

// Cached offline fallback, read once at module load so a reload shows the last
// known value before the first evt:status arrives.
function readCache() {
  try {
    return localStorage.getItem(STANDBY_ON_BREW_KEY) === 'true';
  } catch {
    return false;
  }
}

function writeCache(value) {
  try {
    localStorage.setItem(STANDBY_ON_BREW_KEY, String(value));
  } catch (error) {
    console.warn('Failed to persist standby-on-brew setting:', error);
  }
}

// Offline view of the setting. Seeded from localStorage and updated on every
// (also disconnected) toggle so the UI reacts immediately while disconnected.
const offlineStandbyOnBrew = signal(readCache());

// Device-authoritative when connected (PRO-545). The firmware broadcasts the
// canonical standby-on-brew flag as `sb` in evt:status (mapped to
// status.standbyOnBrewEnabled). localStorage is demoted to an offline-only seed
// that is also kept in sync as a mirror for the next reload. Mirrors
// useAutoSteam exactly.
const standbyOnBrewSignal = computed(() => {
  if (machine.value.connected) {
    return !!machine.value.status.standbyOnBrewEnabled;
  }
  return offlineStandbyOnBrew.value;
});

export function useStandbyOnBrew() {
  const api = useContext(ApiServiceContext);
  const standbyOnBrewEnabled = standbyOnBrewSignal.value;

  const toggleStandbyOnBrew = useCallback(() => {
    const next = !standbyOnBrewSignal.value;
    // Device-authoritative: a toggle issued while disconnected is not persisted
    // on the device and will be overwritten by the next evt:status on reconnect.
    // Intentional — the device is the source of truth. We still mirror locally
    // so the offline view flips immediately and survives a reload before the
    // device echoes it back.
    offlineStandbyOnBrew.value = next;
    writeCache(next);
    try {
      // Fire-and-forget: firmware sends no res: reply, just rebroadcasts `sb`.
      // Send a real boolean per the firmware type-gate.
      api?.send({ tp: 'req:standby-on-brew:set', enabled: next });
    } catch (error) {
      console.error('Failed to send standby-on-brew toggle:', error);
    }
  }, [api]);

  return { standbyOnBrewEnabled, toggleStandbyOnBrew };
}
