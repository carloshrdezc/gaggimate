// Download wiring for the Beanconqueror export (PRO-632).
//
// Kept apart from `beanconquerorExport.js` so that module stays pure (no DOM, no
// blobs escaping into globals) and can be unit-tested without a browser. Mirrors
// the seam `pages/ShotHistory/historyExport.js` already uses: serialize, then
// hand a Blob to `utils/download.js`.

import { downloadBlob } from '../download.js';
import { buildBeanconquerorZip, toBeanconquerorBackup } from './beanconquerorExport.js';

/**
 * Serializes beans + shots into a Beanconqueror backup and downloads it as
 * `Beanconqueror.zip`.
 *
 * @param {{ beans?: Array<object>, shots?: Array<object> }} data
 * @param {{ targetWindow?: Window|null }} [options] forwarded to `downloadBlob`
 *   so a deferred (popup) download started before the async work still lands.
 * @returns {Promise<{ filename: string, beanCount: number, brewCount: number }>}
 */
export async function downloadBeanconquerorBackup(data, options = {}) {
  const backup = toBeanconquerorBackup(data);
  const { blob, filename } = await buildBeanconquerorZip(backup);

  downloadBlob(blob, filename, options);

  return { filename, beanCount: backup.BEANS.length, brewCount: backup.BREWS.length };
}
