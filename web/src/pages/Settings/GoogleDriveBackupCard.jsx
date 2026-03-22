import { useCallback, useMemo, useState } from 'preact/hooks';
import Card from '../../components/Card.jsx';
import { Spinner } from '../../components/Spinner.jsx';
import { createBackupBundle, restoreBackupBundle } from '../../utils/backupBundle.js';
import {
  downloadGoogleDriveBackup,
  getStoredGoogleDriveClientId,
  listGoogleDriveBackups,
  setStoredGoogleDriveClientId,
  uploadGoogleDriveBackup,
} from '../../utils/googleDriveBackup.js';

export function GoogleDriveBackupCard({ apiService, onRestoreComplete }) {
  const [clientId, setClientId] = useState(() => getStoredGoogleDriveClientId());
  const [busyAction, setBusyAction] = useState('');
  const [status, setStatus] = useState('');
  const [latestBackup, setLatestBackup] = useState(null);

  const hasClientId = clientId.trim().length > 0;
  const isBusy = busyAction.length > 0;

  const latestBackupLabel = useMemo(() => {
    if (!latestBackup?.modifiedTime) return 'No backup checked yet';
    return new Date(latestBackup.modifiedTime).toLocaleString();
  }, [latestBackup]);

  const refreshLatestBackup = useCallback(async () => {
    if (!clientId.trim()) return null;
    const files = await listGoogleDriveBackups(clientId.trim());
    const latest = files[0] || null;
    setLatestBackup(latest);
    return latest;
  }, [clientId]);

  const saveClientId = useCallback(() => {
    if (!clientId.trim()) {
      setStatus('Enter a Google OAuth client ID first.');
      return;
    }
    setStoredGoogleDriveClientId(clientId.trim());
    setStatus('Google Drive client ID saved in this browser.');
  }, [clientId]);

  const handleBackup = useCallback(async () => {
    if (!apiService) {
      setStatus('WebSocket is not connected yet.');
      return;
    }

    setBusyAction('backup');
    setStatus('Building backup bundle...');
    try {
      setStoredGoogleDriveClientId(clientId.trim());
      const bundle = await createBackupBundle(apiService);
      setStatus('Uploading backup to Google Drive...');
      await uploadGoogleDriveBackup(clientId.trim(), bundle);
      const latest = await refreshLatestBackup();
      setStatus(
        latest
          ? `Backup saved to Google Drive at ${new Date(latest.modifiedTime).toLocaleString()}.`
          : 'Backup saved to Google Drive.',
      );
    } catch (error) {
      console.error('Google Drive backup failed:', error);
      setStatus(`Backup failed: ${error.message}`);
    } finally {
      setBusyAction('');
    }
  }, [apiService, clientId, refreshLatestBackup]);

  const handleRestore = useCallback(async () => {
    if (!apiService) {
      setStatus('WebSocket is not connected yet.');
      return;
    }

    setBusyAction('restore');
    setStatus('Looking for the latest Google Drive backup...');
    try {
      setStoredGoogleDriveClientId(clientId.trim());
      const latest = (await refreshLatestBackup()) || null;
      if (!latest?.id) {
        throw new Error('No backup file was found in Google Drive app storage.');
      }

      const confirmed = confirm(
        `Restore the latest Google Drive backup from ${new Date(latest.modifiedTime).toLocaleString()}?`,
      );
      if (!confirmed) {
        setStatus('Restore cancelled.');
        return;
      }

      setStatus('Downloading backup from Google Drive...');
      const bundle = await downloadGoogleDriveBackup(clientId.trim(), latest.id);
      setStatus('Restoring backup data...');
      await restoreBackupBundle(apiService, bundle);
      setStatus('Restore completed. Reloading current settings...');
      onRestoreComplete?.();
    } catch (error) {
      console.error('Google Drive restore failed:', error);
      setStatus(`Restore failed: ${error.message}`);
    } finally {
      setBusyAction('');
    }
  }, [apiService, clientId, onRestoreComplete, refreshLatestBackup]);

  return (
    <Card sm={10} lg={5} title='Google Drive Backup'>
      <div className='space-y-4'>
        <div className='text-sm leading-relaxed text-base-content/70'>
          Save and restore settings, profiles, beans, and shot history backups using a private file
          in your Google Drive app storage.
        </div>

        <div className='form-control'>
          <label htmlFor='googleDriveClientId' className='mb-2 block text-sm font-medium'>
            Google OAuth Client ID
          </label>
          <input
            id='googleDriveClientId'
            type='text'
            className='input input-bordered w-full'
            placeholder='1234567890-abc123.apps.googleusercontent.com'
            value={clientId}
            onChange={e => setClientId(e.target.value)}
          />
          <div className='mt-2 text-xs text-base-content/60'>
            Stored in this browser only. Use a Web application OAuth client with the Google Drive
            API enabled.
          </div>
        </div>

        <div className='flex flex-wrap gap-2'>
          <button
            type='button'
            className='btn btn-outline btn-sm'
            onClick={saveClientId}
            disabled={isBusy}
          >
            Save Client ID
          </button>
          <button
            type='button'
            className='btn btn-primary btn-sm'
            onClick={handleBackup}
            disabled={!hasClientId || isBusy}
          >
            {busyAction === 'backup' ? <Spinner size={4} /> : null}
            Backup Now
          </button>
          <button
            type='button'
            className='btn btn-secondary btn-sm'
            onClick={handleRestore}
            disabled={!hasClientId || isBusy}
          >
            {busyAction === 'restore' ? <Spinner size={4} /> : null}
            Restore Latest
          </button>
        </div>

        <div className='rounded-xl border border-base-content/10 bg-base-100/60 px-4 py-3 text-sm'>
          <div className='font-medium'>Latest Drive backup</div>
          <div className='mt-1 text-base-content/70'>{latestBackupLabel}</div>
          {latestBackup?.size ? (
            <div className='mt-1 text-xs text-base-content/60'>{Number(latestBackup.size)} bytes</div>
          ) : null}
        </div>

        {status ? (
          <div className='rounded-xl border border-base-content/10 bg-base-100/60 px-4 py-3 text-sm'>
            {status}
          </div>
        ) : null}
      </div>
    </Card>
  );
}
