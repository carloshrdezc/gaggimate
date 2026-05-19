/**
 * IndexedDBService.js
 *
 * Browser-side storage for uploaded shots and profiles
 * These are temporary uploads for analysis and NOT saved to GaggiMate controller
 */

import { openDB } from 'idb';

const DB_NAME = 'gaggimate-analyzer';
const DB_VERSION = 2;

/**
 * Thrown by storage-write methods when the browser refuses the write because
 * the IndexedDB quota for this origin is exhausted. Lets callers show a
 * "browser storage full" affordance instead of a generic failure.
 */
export class BrowserStorageFullError extends Error {
  constructor(message = 'Browser storage is full. Free up space and try again.') {
    super(message);
    this.name = 'BrowserStorageFullError';
  }
}

/**
 * Returns true if the error is a recoverable IndexedDB connection state error
 * (e.g. the underlying connection was closed by the browser, or another tab
 * triggered a versionchange that closed it). These can be cleared by reopening
 * the database.
 */
function isRecoverableConnectionError(err) {
  if (!err) return false;
  // DOMException name-based detection (works across browsers, including Safari).
  if (err.name === 'InvalidStateError') return true;
  // idb wraps some failures with a message; match defensively.
  if (typeof err.message === 'string' && err.message.includes('database connection is closing')) {
    return true;
  }
  return false;
}

/** Returns true if the error indicates the browser storage quota is exhausted. */
function isQuotaError(err) {
  if (!err) return false;
  return err.name === 'QuotaExceededError' || err.code === 22;
}

class IndexedDBService {
  constructor() {
    this.db = null;
    this._initPromise = null;
  }

  /**
   * Initialize the database.
   * Caches the init promise to prevent concurrent openDB calls.
   */
  async init() {
    if (this.db) return this.db;
    if (this._initPromise) return this._initPromise;

    this._initPromise = openDB(DB_NAME, DB_VERSION, {
      upgrade(db) {
        if (!db.objectStoreNames.contains('shots')) {
          db.createObjectStore('shots', { keyPath: 'name' });
        }
        if (!db.objectStoreNames.contains('profiles')) {
          db.createObjectStore('profiles', { keyPath: 'name' });
        }
        // v2: Dedicated notes store (same JSON format as GaggiMate API)
        if (!db.objectStoreNames.contains('notes')) {
          db.createObjectStore('notes', { keyPath: 'id' });
        }
      },
      blocked() {
        // Another tab holds an older-version connection. Caller can still
        // proceed; the open promise will resolve once that tab closes.
        console.warn('IndexedDB upgrade blocked by another open connection.');
      },
      terminated: () => {
        // Browser killed the connection (e.g. profile cleared, tab evicted).
        // Drop our cached references so the next call reopens.
        this.db = null;
        this._initPromise = null;
      },
    })
      .then(db => {
        this.db = db;
        return db;
      })
      .catch(err => {
        this._initPromise = null;
        throw err;
      });

    return this._initPromise;
  }

  /**
   * Run a database operation with reopen-on-failure semantics.
   *
   * If the operation throws an `InvalidStateError` (typically from a connection
   * that was closed by the browser or another tab), drop the cached connection
   * and retry once.
   *
   * @template T
   * @param {(db: IDBDatabase) => Promise<T>} fn
   * @returns {Promise<T>}
   */
  async _withDb(fn) {
    let db = await this.init();
    try {
      return await fn(db);
    } catch (err) {
      if (!isRecoverableConnectionError(err)) {
        throw err;
      }
      // Connection was stale; null and retry once.
      console.warn('IndexedDB connection lost, reopening:', err.message);
      this.db = null;
      this._initPromise = null;
      db = await this.init();
      return await fn(db);
    }
  }

  /**
   * Save a shot to browser storage.
   * @param {Object} shot - Shot data with metadata
   * @throws {BrowserStorageFullError} when the browser quota is exhausted.
   */
  async saveShot(shot) {
    const storageKey = String(shot.storageKey || shot.name || shot.id || Date.now());

    // Add source tag and storage timestamp
    const shotWithMeta = {
      ...shot,
      name: storageKey,
      storageKey,
      source: 'browser',
      uploadedAt: Date.now(),
    };

    try {
      await this._withDb(db => db.put('shots', shotWithMeta));
    } catch (err) {
      if (isQuotaError(err)) {
        throw new BrowserStorageFullError();
      }
      throw err;
    }
    return shotWithMeta;
  }

  /**
   * Get all browser-uploaded shots.
   * @returns {Array} List of shots
   */
  async getAllShots() {
    const shots = await this._withDb(db => db.getAll('shots'));

    // Ensure all have source tag
    return shots.map(shot => ({
      ...shot,
      storageKey: shot.storageKey || shot.name || String(shot.id || ''),
      source: 'browser',
    }));
  }

  /**
   * Get a single shot by name.
   * @param {string} name - Shot filename/ID
   */
  async getShot(name) {
    return this._withDb(db => db.get('shots', name));
  }

  /**
   * Close the database connection.
   *
   * Note: callers normally do NOT need to call this - the singleton keeps the
   * connection open for the lifetime of the page. Provided for tests and
   * explicit teardown.
   */
  async close() {
    if (this.db) {
      this.db.close();
      this.db = null;
      this._initPromise = null;
    }
  }

  /**
   * Clear all browser storage.
   *
   * Keeps the connection open; subsequent reads/writes work without
   * reinitializing. (Previously this called `close()` which left concurrent
   * callers holding a closed-DB reference and throwing `InvalidStateError` on
   * their next operation.)
   */
  async clearAll() {
    await this._withDb(async db => {
      const tx = db.transaction(['shots', 'profiles', 'notes'], 'readwrite');
      await Promise.all([
        tx.objectStore('shots').clear(),
        tx.objectStore('profiles').clear(),
        tx.objectStore('notes').clear(),
        tx.done,
      ]);
    });
  }

  /**
   * Delete a shot from browser storage.
   * @param {string} name - Shot filename/ID
   */
  async deleteShot(name) {
    await this._withDb(db => db.delete('shots', name));
  }

  /**
   * Save a profile to browser storage.
   * @param {Object} profile - Profile data
   * @throws {BrowserStorageFullError} when the browser quota is exhausted.
   */
  async saveProfile(profile) {
    // Add source tag and storage timestamp
    const profileWithMeta = {
      ...profile,
      source: 'browser',
      uploadedAt: Date.now(),
    };

    try {
      await this._withDb(db => db.put('profiles', profileWithMeta));
    } catch (err) {
      if (isQuotaError(err)) {
        throw new BrowserStorageFullError();
      }
      throw err;
    }
    return profileWithMeta;
  }

  /**
   * Get all browser-uploaded profiles.
   * @returns {Array} List of profiles
   */
  async getAllProfiles() {
    const profiles = await this._withDb(db => db.getAll('profiles'));

    // Ensure all have source tag
    return profiles.map(profile => ({
      ...profile,
      source: 'browser',
    }));
  }

  /**
   * Get a single profile by name.
   * @param {string} name - Profile filename/ID
   */
  async getProfile(name) {
    return this._withDb(db => db.get('profiles', name));
  }

  /**
   * Delete a profile from browser storage.
   * @param {string} name - Profile filename/ID
   */
  async deleteProfile(name) {
    await this._withDb(db => db.delete('profiles', name));
  }

  /**
   * Save notes for a shot.
   * @param {Object} notes - Notes object with id, rating, beanType, etc.
   * @throws {BrowserStorageFullError} when the browser quota is exhausted.
   */
  async saveNotes(notes) {
    try {
      await this._withDb(db => db.put('notes', notes));
    } catch (err) {
      if (isQuotaError(err)) {
        throw new BrowserStorageFullError();
      }
      throw err;
    }
  }

  /**
   * Get notes for a shot by ID.
   * @param {string} id - Shot ID
   * @returns {Object|undefined} Notes object or undefined
   */
  async getNotes(id) {
    return this._withDb(db => db.get('notes', id));
  }

  /**
   * Delete notes for a shot.
   * @param {string} id - Shot ID
   */
  async deleteNotes(id) {
    await this._withDb(db => db.delete('notes', id));
  }

  /**
   * Get storage stats.
   * @returns {Object} Count of shots and profiles
   */
  async getStats() {
    return this._withDb(async db => {
      const shotCount = await db.count('shots');
      const profileCount = await db.count('profiles');
      return {
        shots: shotCount,
        profiles: profileCount,
        total: shotCount + profileCount,
      };
    });
  }
}

// Export singleton instance
export const indexedDBService = new IndexedDBService();
