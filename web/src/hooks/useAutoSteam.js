import { useCallback, useContext } from 'preact/hooks';
import { computed, signal } from '@preact/signals';
import { ApiServiceContext, machine } from '../services/ApiService.js';

const AUTO_STEAM_KEY = 'gaggimate-auto-steam';

// Cached offline fallback, read once at module load so a reload shows the last
// known value before the first evt:status arrives.
function readCache() {
  try {
    return localStorage.getItem(AUTO_STEAM_KEY) === 'true';
  } catch {
    return false;
  }
}

function writeCache(value) {
  try {
    localStorage.setItem(AUTO_STEAM_KEY, String(value));
  } catch (error) {
    console.warn('Failed to persist auto-steam setting:', error);
  }
}

// Offline view of the setting. Seeded from localStorage and updated on every
// (also disconnected) toggle so the UI reacts immediately while disconnected.
const offlineAutoSteam = signal(readCache());

// Device-authoritative when connected (PRO-226). The firmware broadcasts the
// canonical auto-steam flag as `as` in evt:status (mapped to
// status.autoSteamEnabled). localStorage is demoted to an offline-only seed
// that is also kept in sync as a mirror for the next reload.
const autoSteamSignal = computed(() => {
  if (machine.value.connected) {
    return !!machine.value.status.autoSteamEnabled;
  }
  return offlineAutoSteam.value;
});

export function useAutoSteam() {
  const api = useContext(ApiServiceContext);
  const autoSteamEnabled = autoSteamSignal.value;

  const toggleAutoSteam = useCallback(() => {
    const next = !autoSteamSignal.value;
    // Mirror immediately so a disconnected toggle survives a reload and so the
    // offline view flips right away even before the device echoes it back.
    offlineAutoSteam.value = next;
    writeCache(next);
    try {
      // Fire-and-forget: firmware sends no res: reply, just rebroadcasts `as`.
      // Send a real boolean per the firmware type-gate.
      api?.send({ tp: 'req:autosteam:set', enabled: next });
    } catch (error) {
      console.error('Failed to send auto-steam toggle:', error);
    }
  }, [api]);

  return { autoSteamEnabled, toggleAutoSteam };
}
