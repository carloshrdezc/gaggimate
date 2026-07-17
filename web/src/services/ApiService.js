import { createContext } from 'preact';
import { signal } from '@preact/signals';
import uuidv4 from '../utils/uuid.js';

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
const LOCAL_AUTH_TOKEN_KEY = 'gaggimate_local_admin_token';

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

  constructor() {
    // Bind methods once to avoid creating new function references on each reconnect
    this._boundOnMessage = this._onMessage.bind(this);
    this._boundOnClose = this._onClose.bind(this);
    this._boundOnError = this._onError.bind(this);
    this._boundOnOpen = this._onOpen.bind(this);

    console.log('Established websocket connection');
    this.connect();
  }

  _resolveWsUrl() {
    // Check URL params for relay configuration (one-time setup link)
    const params = new URLSearchParams(window.location.search);
    const relayParam = params.get('relay');
    const tokenParam = params.get('token');
    if (relayParam && tokenParam) {
      localStorage.setItem('gaggimate_relay_url', relayParam);
      localStorage.setItem('gaggimate_relay_token', tokenParam);
      // Clean params from URL without reload
      const url = new URL(window.location.href);
      url.searchParams.delete('relay');
      url.searchParams.delete('token');
      history.replaceState(null, '', url.toString());
    }

    const relayUrl = localStorage.getItem('gaggimate_relay_url');
    const relayToken = localStorage.getItem('gaggimate_relay_token');
    if (relayUrl && relayToken) {
      return `${relayUrl}/connect?token=${encodeURIComponent(relayToken)}&role=browser`;
    }

    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    return `${wsProtocol}${window.location.host}/ws`;
  }

  async connect() {
    if (this.isConnecting) return;
    this.isConnecting = true;

    try {
      if (this.socket) {
        // Remove old listeners before closing to prevent memory leaks
        this.socket.removeEventListener('message', this._boundOnMessage);
        this.socket.removeEventListener('close', this._boundOnClose);
        this.socket.removeEventListener('error', this._boundOnError);
        this.socket.removeEventListener('open', this._boundOnOpen);
        this.socket.close();
      }

      this.socket = new WebSocket(this._resolveWsUrl());

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

  _onOpen() {
    console.log('WebSocket connected successfully');
    const localAdminToken = localStorage.getItem(LOCAL_AUTH_TOKEN_KEY);
    this.authenticateLocal(localAdminToken);
    this.reconnectAttempts = 0;
    this.isConnecting = false;
    machine.value = {
      ...machine.value,
      connected: true,
    };
  }

  _onClose() {
    console.log('WebSocket connection closed');
    this.isConnecting = false; // Reset flag to allow reconnection
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
    if (this.socket) {
      this.socket.close();
    }
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
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
    }

    // Calculate delay with exponential backoff
    const delay = Math.min(
      this.baseReconnectDelay * Math.pow(2, this.reconnectAttempts),
      this.maxReconnectDelay,
    );

    console.log(`Scheduling reconnect attempt ${this.reconnectAttempts + 1} in ${delay}ms`);

    this.reconnectTimeout = setTimeout(() => {
      this.reconnectAttempts++;
      this.connect();
    }, delay);
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
      brewTarget: !!message.bt,
      brewTargetDuration: message.btd || 0,
      brewTargetVolume: message.btv || 0,
      allowYieldOverride: !!message.ayo,
      // PRO-226: device-authoritative auto-steam (as: 0/1) and brew dose (dg: float).
      autoSteamEnabled: !!message.as,
      // Legacy (pre-PRO-225) firmware doesn't send `dg`; emit null so the
      // consumer falls back to its localStorage cache instead of clobbering it.
      doseGrams: Number.isFinite(message.dg) ? message.dg : null,
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

export const machine = signal({
  connected: false,
  status: {
    currentTemperature: 0,
    targetTemperature: 0,
    mode: 0,
    selectedProfile: '',
    selectedProfileId: null,
    selectedBean: '',
    brewTargetDuration: 0,
    brewTargetVolume: 0,
    allowYieldOverride: false,
    autoSteamEnabled: false,
    doseGrams: DEFAULT_DOSE_GRAMS,
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
