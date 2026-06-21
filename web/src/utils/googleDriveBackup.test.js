import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { createGoogleDriveProvider } from './googleDriveBackup.js';

const GIS_SCRIPT_URL = 'https://accounts.google.com/gsi/client';

describe('GoogleDriveProvider', () => {

  describe('isConfigured', () => {
    it('returns false when clientId is empty', () => {
      const provider = createGoogleDriveProvider({ clientId: '' });
      expect(provider.isConfigured()).toBe(false);
    });

    it('returns false when clientId is only whitespace', () => {
      const provider = createGoogleDriveProvider({ clientId: '   ' });
      expect(provider.isConfigured()).toBe(false);
    });

    it('returns true when clientId is set', () => {
      const provider = createGoogleDriveProvider({ clientId: 'test-id-123' });
      expect(provider.isConfigured()).toBe(true);
    });
  });

  describe('providerId and providerName', () => {
    it('has correct providerId', () => {
      const provider = createGoogleDriveProvider({ clientId: 'test' });
      expect(provider.providerId).toBe('google-drive');
    });

    it('has correct providerName', () => {
      const provider = createGoogleDriveProvider({ clientId: 'test' });
      expect(provider.providerName).toBe('Google Drive');
    });
  });

  // Token-requiring tests: ensureGISLoaded() -> loadScript() would append the
  // real GIS <script> and await its load event, which never fires under jsdom
  // (the request hangs and the test times out). We pre-insert a fake
  // already-loaded GIS script so loadScript short-circuits via its
  // existing-element branch, then mock window.google for the token client.
  describe('authorized operations', () => {
    let originalFetch;
    let gisScript;

    beforeEach(() => {
      originalFetch = window.fetch;
      gisScript = document.createElement('script');
      gisScript.src = GIS_SCRIPT_URL;
      gisScript.dataset.loaded = 'true';
      document.head.appendChild(gisScript);

      window.google = {
        accounts: {
          oauth2: {
            initTokenClient: ({ callback }) => {
              callback({ access_token: 'fake-access-token', expires_in: 3600 });
            },
          },
        },
      };
    });

    afterEach(() => {
      window.fetch = originalFetch;
      delete window.google;
      gisScript.remove();
    });

    describe('listBackups', () => {
      it('returns empty array when no files exist', async () => {
        window.fetch = async () => ({
          ok: true,
          json: async () => ({ files: [] }),
        });

        const provider = createGoogleDriveProvider({ clientId: 'test' });
        const result = await provider.listBackups();
        expect(result).toEqual([]);
      });

      it('returns mapped files with fileId, modifiedTime, size', async () => {
        const mockFiles = [
          { id: 'file-1', modifiedTime: '2025-01-01T00:00:00Z', size: 1234 },
          { id: 'file-2', modifiedTime: '2025-01-02T00:00:00Z', size: 5678 },
        ];
        window.fetch = async () => ({
          ok: true,
          json: async () => ({ files: mockFiles }),
        });

        const provider = createGoogleDriveProvider({ clientId: 'test' });
        const result = await provider.listBackups();

        expect(result).toEqual([
          { fileId: 'file-1', modifiedTime: '2025-01-01T00:00:00Z', size: 1234 },
          { fileId: 'file-2', modifiedTime: '2025-01-02T00:00:00Z', size: 5678 },
        ]);
      });
    });

    describe('uploadBackup', () => {
      // uploadBackup first lists existing backups (to decide create vs update),
      // then performs the upload. The list endpoint must return a files array;
      // the upload endpoint (/upload/) returns the uploaded file metadata.
      it('uploads bundle and returns fileId and modifiedTime', async () => {
        window.fetch = async url => {
          if (String(url).includes('/upload/')) {
            return {
              ok: true,
              json: async () => ({ id: 'uploaded-file-id', modifiedTime: '2025-01-01T00:00:00Z' }),
            };
          }
          return { ok: true, json: async () => ({ files: [] }) };
        };
        const provider = createGoogleDriveProvider({ clientId: 'test' });
        const bundle = { type: 'gaggimate-backup', version: 2 };
        const result = await provider.uploadBackup(bundle);
        expect(result.fileId).toBe('uploaded-file-id');
        expect(result.modifiedTime).toBe('2025-01-01T00:00:00Z');
      });

      it('throws when upload does not return a file ID', async () => {
        window.fetch = async url => {
          if (String(url).includes('/upload/')) {
            return { ok: true, json: async () => ({}) };
          }
          return { ok: true, json: async () => ({ files: [] }) };
        };
        const provider = createGoogleDriveProvider({ clientId: 'test' });
        const bundle = { type: 'gaggimate-backup', version: 2 };
        let threw = false;
        try {
          await provider.uploadBackup(bundle);
        } catch (e) {
          threw = true;
          expect(e.message).toBe('Upload succeeded but did not return a file ID.');
        }
        expect(threw).toBe(true);
      });
    });

    describe('downloadBackup', () => {
      it('downloads and parses backup content for given fileId', async () => {
        const mockBundle = { type: 'gaggimate-backup', version: 2, settings: {} };
        window.fetch = async () => ({
          ok: true,
          json: async () => mockBundle,
        });
        const provider = createGoogleDriveProvider({ clientId: 'test' });
        const result = await provider.downloadBackup('file-id-123');
        expect(result).toEqual(mockBundle);
      });
    });
  });

});
