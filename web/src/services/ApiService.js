import { createContext } from 'preact';
import { signal } from '@preact/signals';
import uuidv4 from '../utils/uuid.js';
import { LOCAL_AUTH_TOKEN_KEY } from './localAuthFetch.js';
import {
  beginRelayProvisioning,
  relayCredentials,
  relayWebSocketProtocols,
} from './relayConfig.js';

/**
 * Thrown by `ApiService.request()` when the underlying WebSocket closes (or
 * errors) before a matching response arrives. Lets callers distinguish a
 * transport drop from an in-band timeout or server error.
 */
export class WebSocketDisconnectedError extends Error {
  constructor(message = 'WebSocket disconnected before response was received') {
    super(message);
    this.name = 'WebSocketDisconnectedError';
  }
}

// Default timeout for `request()` calls. Reduced from 30s so users don't sit
// behind dozens of stalled promises after a transient drop. The transport now
// rejects in-flight requests immediately on close/error, so this is only a
// safety net for a server that opened the socket but never responds.
const DEFAULT_REQUEST_TIMEOUT_MS = 5000;

// Default brew dose (grams) used to seed the pre-connection status object.
// Matches the firmware default (Settings.h: doseGrams = 18.0).
const DEFAULT_DOSE_GRAMS = 18;

// Default manual grinder-dial setting (PRO-603). Matches the firmware default
// (Settings.h: manualGrindSetting = 0.0). 0 = "not set yet".
const DEFAULT_MANUAL_GRIND_SETTING = 0;

// If a socket errors before it ever opened, some browsers do NOT emit a
// subsequent `close` event — so relying on `_onClose` to arm the reconnect can
// stall forever. `_onError` arms this short fallback; if `_onClose` hasn't run
// within the window it schedules the reconnect directly. Because
// `_scheduleReconnect` is idempotent (PRO-18), whichever path fires first wins
// and the other is a no-op, so we never double-count.
const ERROR_RECONNECT_FALLBACK_MS = 1000;

/**
 * Internal connection state machine (PRO-18). This is bookkeeping used to keep
 * reconnect scheduling idempotent; it is SEPARATE from the `connectionState`
 * signal that feeds the PRO-7 banner (that signal keeps its
 * 'connected'/'reconnecting' contract unchanged).
 */
export const ConnState = Object.freeze({
  DISCONNECTED: 'DISCONNECTED',
  CONNECTING: 'CONNECTING',
  OPEN: 'OPEN',
  RECONNECTING: 'RECONNECTING',
});

export function validateWebSocketRequest(data) {
  if (
    !data ||
    typeof data !== 'object' ||
    typeof data.tp !== 'string' ||
    !data.tp.startsWith('req:')
  ) {
    throw new TypeError('WebSocket request requires a req:* tp string');
  }
  const numeric = (field, min, max) => {
    if (
      typeof data[field] !== 'number' ||
      !Number.isFinite(data[field]) ||
      data[field] < min ||
      data[field] > max
    ) {
      throw new TypeError(`${field} must be a finite number between ${min} and ${max}`);
    }
  };
  switch (data.tp) {
    case 'req:dose:set':
      numeric('grams', Number.EPSILON, 200);
      break;
    case 'req:manual-grind:set':
      numeric('value', 0, 100);
      break;
    case 'req:change-brew-target':
      numeric('target', 0, 200);
      break;
    case 'req:brew-temperature:set':
      // PRO-630: same bound as the server (StrictValidationPolicy.h and
      // constants.h MIN_TEMP/MAX_TEMP = [0, 160]) so a value the firmware would
      // accept is never blocked client-side. The dashboard control clamps to a
      // narrower, espresso-appropriate UI band before it gets here.
      numeric('temperature', 0, 160);
      break;
    case 'req:change-grind-target':
      numeric('target', 0, 1);
      if (!Number.isInteger(data.target)) throw new TypeError('target must be 0 or 1');
      break;
    case 'req:change-mode':
      numeric('mode', 0, 5);
      if (!Number.isInteger(data.mode)) throw new TypeError('mode must be an integer');
      break;
    case 'req:manual:update':
      if (data.targetType !== undefined && !['flow', 'pressure'].includes(data.targetType))
        throw new TypeError("targetType must be 'flow' or 'pressure'");
      if (data.pressure !== undefined) numeric('pressure', 0, 12);
      if (data.flow !== undefined) numeric('flow', 0, 20);
      if (data.temperature !== undefined) numeric('temperature', 0, 150);
      break;
    default:
      break;
  }
  return data;
}

export default class ApiService {
  static HISTORY_MAX_SIZE = 600;

  socket = null;
  listeners = {};
  /** @type {Map<string, { resolve: Function, reject: Function, timeoutId: ReturnType<typeof setTimeout>, returnType: string }>} */
  _pendingRequests = new Map();
  /** Monotonic counter for listener ids — guaranteed unique within a session. */
  _nextListenerId = 0;
  reconnectAttempts = 0;
  maxReconnectDelay = 30000; // Maximum delay of 30 seconds
  baseReconnectDelay = 1000; // Start with 1 second delay
  reconnectTimeout = null;
  isConnecting = false;
  /**
   * Fallback timer armed by `_onError` when the socket errored before opening,
   * so a missing `close` event can't stall reconnection (PRO-18). Cancelled in
   * `_onClose` when a real close arrives first.
   */
  _errorReconnectFallback = null;
  /** Internal connection state machine (PRO-18). See {@link ConnState}. */
  _connState = ConnState.DISCONNECTED;

  constructor() {
    // Bind methods once to avoid creating new function references on each reconnect
    this._boundOnMessage = this._onMessage.bind(this);
    this._boundOnClose = this._onClose.bind(this);
    this._boundOnError = this._onError.bind(this);
    this._boundOnOpen = this._onOpen.bind(this);

    console.log('Established websocket connection');
    this.connect();
  }

  _resolveWsConfig() {
    beginRelayProvisioning();
    const relay = relayCredentials();
    if (relay)
      return {
        url: `${relay.relayUrl}/connect?role=browser`,
        protocols: relayWebSocketProtocols(relay.relayToken),
      };

    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    return { url: `${wsProtocol}${window.location.host}/ws`, protocols: undefined };
  }

  async connect() {
    if (this.isConnecting) return;
    this.isConnecting = true;
    this._connState = ConnState.CONNECTING;

    try {
      if (this.socket) {
        // Remove old listeners before closing to prevent memory leaks
        this.socket.removeEventListener('message', this._boundOnMessage);
        this.socket.removeEventListener('close', this._boundOnClose);
        this.socket.removeEventListener('error', this._boundOnError);
        this.socket.removeEventListener('open', this._boundOnOpen);
        this.socket.close();
      }

      const { url, protocols } = this._resolveWsConfig();
      this.socket = protocols ? new WebSocket(url, protocols) : new WebSocket(url);

      // Use bound references to enable proper cleanup
      this.socket.addEventListener('message', this._boundOnMessage);
      this.socket.addEventListener('close', this._boundOnClose);
      this.socket.addEventListener('error', this._boundOnError);
      this.socket.addEventListener('open', this._boundOnOpen);
    } catch (error) {
      console.error('WebSocket connection error:', error);
      this.isConnecting = false;
      this._scheduleReconnect();
    }
  }

  authenticateLocal(token) {
    if (token && this.socket && this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify({ tp: 'req:auth', token }));
    }
  }

  /**
   * Send `req:auth` and resolve only once the firmware confirms with a
   * `res:auth {ok, error?}` frame (WebUIPlugin.cpp:1414-1421). Unlike
   * `authenticateLocal` (fire-and-forget, used at `_onOpen` where no user is
   * waiting), this is for interactive callers that must NOT claim success until
   * the device actually accepts the token.
   *
   * `res:auth` carries no `rid`, so it can't ride the `request()` rid-pairing
   * path — we listen on the `res:auth` type bus for the next reply instead.
   *
   * Rejects with:
   * - `WebSocketDisconnectedError` if the socket isn't open at send time (still
   *   connecting/reconnecting — the fire-and-forget path would silently no-op).
   * - `Error('Authentication timed out')` if no `res:auth` arrives in time.
   *
   * Resolves with `{ ok: boolean, error?: string }` — `ok:false` means the
   * device rejected the token (wrong/stale). Callers decide UI from `ok`.
   *
   * @param {string} token
   * @param {number} [timeoutMs]
   * @returns {Promise<{ ok: boolean, error?: string }>}
   */
  authenticateLocalAndConfirm(token, timeoutMs = DEFAULT_REQUEST_TIMEOUT_MS) {
    return new Promise((resolve, reject) => {
      if (!token || !this.socket || this.socket.readyState !== WebSocket.OPEN) {
        reject(new WebSocketDisconnectedError('WebSocket is not connected'));
        return;
      }

      let settled = false;
      let listenerId;
      const timeoutId = setTimeout(() => {
        if (settled) return;
        settled = true;
        this.off('res:auth', listenerId);
        reject(new Error('Authentication timed out'));
      }, timeoutMs);

      listenerId = this.on('res:auth', message => {
        if (settled) return;
        settled = true;
        clearTimeout(timeoutId);
        this.off('res:auth', listenerId);
        resolve({ ok: !!message.ok, error: message.error });
      });

      try {
        this.socket.send(JSON.stringify({ tp: 'req:auth', token }));
      } catch (error) {
        if (settled) return;
        settled = true;
        clearTimeout(timeoutId);
        this.off('res:auth', listenerId);
        reject(error);
      }
    });
  }

  _onOpen() {
    console.log('WebSocket connected successfully');
    const localAdminToken = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
    this.authenticateLocal(localAdminToken);
    this.reconnectAttempts = 0;
    this.isConnecting = false;
    this._connState = ConnState.OPEN;
    // A successful open supersedes any pending reconnect/fallback timers.
    this._clearReconnectTimers();
    // Transport is up: clear the banner (PRO-7). The countdown target is no
    // longer meaningful once we're connected.
    connectionState.value = 'connected';
    nextReconnectAt.value = null;
    machine.value = {
      ...machine.value,
      connected: true,
    };
  }

  _onClose() {
    console.log('WebSocket connection closed');
    this.isConnecting = false; // Reset flag to allow reconnection
    // A real close arrived, so the error-path fallback is no longer needed —
    // cancel it so we don't double-schedule (PRO-18).
    this._cancelErrorReconnectFallback();
    machine.value = {
      ...machine.value,
      connected: false,
    };
    this._rejectPendingRequests(
      new WebSocketDisconnectedError('WebSocket closed before response was received'),
    );
    this._scheduleReconnect();
  }

  _onError(error) {
    console.error('WebSocket error:', error);
    this.isConnecting = false; // Reset flag to allow reconnection
    this._rejectPendingRequests(
      new WebSocketDisconnectedError('WebSocket errored before response was received'),
    );
    // Closing the errored socket normally triggers a `close` event, which
    // schedules the reconnect via _onClose. But if the socket errored before it
    // ever opened, some browsers never emit `close` — which would stall
    // reconnection forever. Arm a short fallback that schedules directly if
    // _onClose hasn't run in time. _scheduleReconnect is idempotent (PRO-18), so
    // whichever fires first wins and the other is a no-op. (PRO-18)
    const neverOpened = !this.socket || this.socket.readyState !== WebSocket.OPEN;
    if (this.socket) {
      this.socket.close();
    }
    if (neverOpened) {
      this._cancelErrorReconnectFallback();
      this._errorReconnectFallback = setTimeout(() => {
        this._errorReconnectFallback = null;
        this._scheduleReconnect();
      }, ERROR_RECONNECT_FALLBACK_MS);
    }
  }

  /** Cancel the error-path reconnect fallback timer if armed (PRO-18). */
  _cancelErrorReconnectFallback() {
    if (this._errorReconnectFallback) {
      clearTimeout(this._errorReconnectFallback);
      this._errorReconnectFallback = null;
    }
  }

  /** Clear both the scheduled reconnect and the error fallback timers (PRO-18). */
  _clearReconnectTimers() {
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    this._cancelErrorReconnectFallback();
  }

  /**
   * Reject every in-flight request with the given error and clear their timeouts.
   * Called when the transport drops so callers don't hang for the full timeout.
   */
  _rejectPendingRequests(error) {
    if (this._pendingRequests.size === 0) return;
    const pending = Array.from(this._pendingRequests.values());
    this._pendingRequests.clear();
    for (const entry of pending) {
      clearTimeout(entry.timeoutId);
      entry.reject(error);
    }
  }

  _scheduleReconnect() {
    // Idempotent: one failure schedules exactly one reconnect. If a timer is
    // already armed and we're already in RECONNECTING, do nothing — don't bump
    // attempts, don't re-arm, don't advance the backoff (PRO-18). A single
    // failure fans out to both _onError->close and _onClose (and the connect()
    // catch path); without this guard those competing calls double-count
    // reconnectAttempts and race two timers. reconnectNow() clears the timer
    // first, so it can still force an immediate reconnect through this guard.
    if (this.reconnectTimeout && this._connState === ConnState.RECONNECTING) {
      return;
    }

    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
    }
    // A scheduled reconnect supersedes the error-path fallback.
    this._cancelErrorReconnectFallback();

    this._connState = ConnState.RECONNECTING;

    // Calculate delay with exponential backoff
    const delay = Math.min(
      this.baseReconnectDelay * Math.pow(2, this.reconnectAttempts),
      this.maxReconnectDelay,
    );

    // Surface reconnect state so the connection-lost banner can show a live
    // countdown until the next attempt (PRO-7). Backoff continues indefinitely
    // up to maxReconnectDelay, so the state stays 'reconnecting' — we never
    // give up on our own.
    connectionState.value = 'reconnecting';
    nextReconnectAt.value = Date.now() + delay;

    console.log(`Scheduling reconnect attempt ${this.reconnectAttempts + 1} in ${delay}ms`);

    this.reconnectTimeout = setTimeout(() => {
      // Clear the handle before reconnecting so a subsequent failure can arm a
      // fresh reconnect through the idempotency guard (PRO-18).
      this.reconnectTimeout = null;
      this.reconnectAttempts++;
      this.connect();
    }, delay);
  }

  /**
   * Force an immediate reconnect, bypassing the backoff delay (PRO-7).
   *
   * Wired to the "Reconnect now" button on the connection-lost banner. Clears
   * any pending scheduled reconnect, resets the exponential backoff so the
   * NEXT failure starts from `baseReconnectDelay` again, and retries right
   * away.
   */
  reconnectNow() {
    // Clear any pending scheduled reconnect AND the error-path fallback so a
    // forced reconnect isn't blocked by the idempotency guard (PRO-18).
    this._clearReconnectTimers();
    this.reconnectAttempts = 0;
    nextReconnectAt.value = null;
    // Clear the in-flight guard so a forced reconnect isn't swallowed if a
    // prior connect() attempt is still marked pending (e.g. a socket that
    // opened but never fired open/close).
    this.isConnecting = false;
    this.connect();
  }

  _onMessage(event) {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch (error) {
      console.warn('Failed to parse WebSocket message:', error);
      return; // Discard malformed messages to avoid crashing the WS handler.
    }

    // Validate message structure
    if (!message || typeof message !== 'object' || !message.tp) {
      console.warn('Invalid message structure:', message);
      return;
    }

    // Resolve any in-flight request waiting on this rid. Done before listener
    // fan-out so request/response traffic doesn't depend on the listener bus.
    if (message.rid && this._pendingRequests.has(message.rid)) {
      const entry = this._pendingRequests.get(message.rid);
      this._pendingRequests.delete(message.rid);
      clearTimeout(entry.timeoutId);
      // Surface in-band errors as a rejection so callers' try/catch fire instead
      // of treating a firmware failure (e.g. "Delete failed") as success.
      if (message.error) {
        entry.reject(new Error(message.error));
      } else {
        entry.resolve(message);
      }
      // Note: we deliberately fall through so listeners still observe the
      // response (some screens listen on res:* in addition to using request()).
    }

    const listeners = Object.values(this.listeners[message.tp] || {});
    if (message.tp === 'evt:status') {
      this._onStatus(message);
    } else if (message.tp === 'evt:relay-status') {
      machine.value = { ...machine.value, deviceConnected: message.deviceConnected ?? true };
      return;
    }
    for (const listener of listeners) {
      try {
        listener(message);
      } catch (error) {
        console.error('Error in message listener:', error);
      }
    }
  }

  send(event) {
    validateWebSocketRequest(event);
    if (this.socket && this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(event));
    } else {
      throw new Error('WebSocket is not connected');
    }
  }

  /**
   * Send a request frame and resolve with the matching response.
   *
   * Rejects with:
   * - `Error(message.error)` if the response carries a truthy in-band `error`
   *   field (e.g. firmware "Delete failed" / "Invalid profile id"), so callers
   *   see failures via their try/catch rather than a success-shaped response.
   * - `WebSocketDisconnectedError` if the socket closes/errors before the
   *   response arrives (no waiting for the timeout).
   * - `Error('Request <tp> timed out')` if no response arrives within
   *   `timeoutMs` (default 5s).
   * - The error thrown by `send()` if the socket isn't open at send time.
   *
   * @param {object} data - frame payload (must include `tp`)
   * @param {number} [timeoutMs] - request timeout in ms
   */
  async request(data = {}, timeoutMs = DEFAULT_REQUEST_TIMEOUT_MS) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      throw new WebSocketDisconnectedError('WebSocket is not connected');
    }

    const returnType = `res:${data.tp.substring(4)}`;
    const rid = uuidv4();
    const message = { ...data, rid };

    return new Promise((resolve, reject) => {
      // Send first; if send() throws synchronously we don't want to leave a
      // pending entry behind.
      try {
        this.send(message);
      } catch (error) {
        reject(error);
        return;
      }

      const timeoutId = setTimeout(() => {
        if (this._pendingRequests.delete(rid)) {
          reject(new Error(`Request ${data.tp} timed out`));
        }
      }, timeoutMs);

      this._pendingRequests.set(rid, { resolve, reject, timeoutId, returnType });
    });
  }

  on(type, listener) {
    // Monotonic counter — collision-free, no PRNG, no truncation.
    const id = `l${++this._nextListenerId}`;
    if (!this.listeners[type]) {
      this.listeners[type] = {};
    }
    this.listeners[type][id] = listener;
    return id;
  }

  off(type, id) {
    if (this.listeners[type]) {
      delete this.listeners[type][id];
    }
  }

  /**
   * Adds an entry to history with a fixed maximum size.
   * Uses slice instead of shift to avoid O(n) re-indexing on every status update.
   * @param {Array} history - The current history array
   * @param {Object} entry - The new entry to add
   * @returns {Array} The updated history array
   */
  _addToHistory(history, entry) {
    if (history.length >= ApiService.HISTORY_MAX_SIZE) {
      // Trim the oldest 10% in one slice to amortize the copy cost
      const trimCount = Math.max(1, Math.floor(ApiService.HISTORY_MAX_SIZE * 0.1));
      const trimmed = history.slice(trimCount);
      trimmed.push(entry);
      return trimmed;
    }
    // Under the limit: append without copying the full array
    return [...history, entry];
  }

  _onStatus(message) {
    const newStatus = {
      currentTemperature: message.ct,
      targetTemperature: message.tt,
      currentPressure: message.pr,
      targetPressure: message.pt ?? null,
      targetFlow: message.tf ?? null,
      targetWeight: message.tw || 0,
      activeTargetWeight: (message?.process?.a && message.tw) || 0,
      currentFlow: message.fl,
      mode: message.m,
      selectedProfile: message.p,
      selectedProfileId: message.puid,
      selectedBean: message.bn || '',
      selectedGrinder: message.gr || '',
      brewTarget: !!message.bt,
      brewTargetDuration: message.btd || 0,
      brewTargetVolume: message.btv || 0,
      allowYieldOverride: !!message.ayo,
      // PRO-226: device-authoritative auto-steam (as: 0/1) and brew dose (dg: float).
      autoSteamEnabled: !!message.as,
      // PRO-545: device-authoritative standby-on-brew (sb: 0/1).
      standbyOnBrewEnabled: !!message.sb,
      // Legacy (pre-PRO-225) firmware doesn't send `dg`; emit null so the
      // consumer falls back to its localStorage cache instead of clobbering it.
      doseGrams: Number.isFinite(message.dg) ? message.dg : null,
      // PRO-603: device-authoritative manual grinder-dial setting (mg). Legacy
      // firmware doesn't send `mg`; emit null so the consumer falls back to its
      // localStorage cache instead of clobbering it.
      manualGrindSetting: Number.isFinite(message.mg) ? message.mg : null,
      // PRO-630: device-authoritative selected-profile brew-temperature target
      // (`bto`) and its provenance (`bte`, 1 when an explicit override is
      // persisted for the selected profile). Firmware older than PRO-629 sends
      // neither; emit null / false rather than a fabricated default (e.g. 93) so
      // the dashboard control degrades to a read-only placeholder instead of
      // claiming a target the device never reported.
      brewTemperatureOverrideTarget: Number.isFinite(message.bto) ? message.bto : null,
      brewTemperatureOverrideEnabled: message.bte === 1 || message.bte === true,
      volumetricAvailable: message.bta || false,
      grindTargetDuration: message.gtd || 0,
      grindTargetVolume: message.gtv || 0,
      grindTarget: message.gt || 0,
      grindActive: message.gact || false,
      manualTargetType: message.mtp || 'pressure',
      manualPressure: Number.isFinite(message.mp) ? message.mp : 9,
      manualFlow: Number.isFinite(message.mf) ? message.mf : 2,
      manualTemperature: Number.isFinite(message.mt) ? message.mt : 93,
      currentWeight: message.cw || 0,
      bluetoothConnected: message.bc || false,
      process: message.process || null,
      timestamp: new Date(),
      rssi: message.rssi || 0,
    };
    const historyEntry = { ...newStatus };
    delete historyEntry.process;

    // Efficient history management using circular buffer approach
    const newHistory = this._addToHistory(machine.value.history, historyEntry);

    machine.value = {
      ...machine.value,
      connected: true,
      status: {
        ...machine.value.status,
        ...newStatus,
      },
      capabilities: {
        ...machine.value.capabilities,
        dimming: message.cd,
        pressure: message.cp,
        ledControl: message.led,
      },
      history: newHistory,
    };
  }
}

export const ApiServiceContext = createContext(null);

/**
 * Connection-lost banner state (PRO-7).
 *
 * - `'connected'`    — transport is up; banner hidden.
 * - `'reconnecting'` — socket dropped and a reconnect is scheduled/in-flight.
 *
 * `'failed'` is reserved for a future attempt cap; today the backoff continues
 * up to `maxReconnectDelay` and never gives up, so the state stays
 * `'reconnecting'` while offline. Starts optimistic so a stable connection
 * never flashes the banner during the initial handshake.
 */
export const connectionState = signal('connected');

/**
 * Epoch-ms timestamp of the next scheduled reconnect attempt, or `null` when
 * connected / retrying immediately. The banner derives its live countdown from
 * this (PRO-7).
 */
export const nextReconnectAt = signal(null);

export const machine = signal({
  connected: false,
  status: {
    currentTemperature: 0,
    targetTemperature: 0,
    mode: 0,
    selectedProfile: '',
    selectedProfileId: null,
    selectedBean: '',
    selectedGrinder: '',
    brewTargetDuration: 0,
    brewTargetVolume: 0,
    allowYieldOverride: false,
    autoSteamEnabled: false,
    standbyOnBrewEnabled: false,
    doseGrams: DEFAULT_DOSE_GRAMS,
    manualGrindSetting: DEFAULT_MANUAL_GRIND_SETTING,
    // PRO-630: no pre-connection default on purpose. The selected-profile brew
    // temperature is device-authoritative; until an evt:status carrying `bto`
    // arrives there is nothing truthful to show, so the dashboard renders a
    // read-only placeholder rather than a fabricated 93 °C.
    brewTemperatureOverrideTarget: null,
    brewTemperatureOverrideEnabled: false,
    grindTargetDuration: 0,
    grindTargetVolume: 0,
    grindTarget: 0,
    grindActive: false,
    manualTargetType: 'pressure',
    manualPressure: 9,
    manualFlow: 2,
    manualTemperature: 93,
    process: null,
  },
  capabilities: {
    pressure: false,
    dimming: false,
  },
  history: [],
});
