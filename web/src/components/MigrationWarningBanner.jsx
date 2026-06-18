import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faTriangleExclamation } from '@fortawesome/free-solid-svg-icons/faTriangleExclamation';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';

/**
 * Prominent pre-upgrade warning shown on the OTA / System & Updates page.
 *
 * This banner is the SAFETY MECHANISM for the SPIFFS->LittleFS migration
 * (PRO-218). Older firmware stores profiles on a SPIFFS partition; the new
 * firmware mounts LittleFS and CLEAN-FORMATS an incompatible SPIFFS partition
 * on first boot (seeding only the Default profile). There is intentionally NO
 * in-place on-device data rescue — the safe migration path is to export
 * profiles OFF the device first, then re-import them after flashing.
 *
 * Because the data lives off-device across the format boundary, a silent wipe
 * is structurally impossible — provided the user exports first. This banner
 * exists to make that step impossible to miss.
 */
export function MigrationWarningBanner() {
  return (
    <div
      role='alert'
      className='rounded-lg border-2 border-[var(--color-warning,#d4a843)] bg-[rgba(212,168,67,0.10)] p-4'
    >
      <div className='flex flex-row items-start gap-3'>
        <FontAwesomeIcon
          icon={faTriangleExclamation}
          className='mt-1 text-[18px] text-[var(--color-warning,#d4a843)]'
        />
        <div className='flex flex-col gap-2'>
          <span className='font-nd-mono text-[14px] font-semibold tracking-[0.08em] text-[var(--color-warning,#d4a843)] uppercase'>
            Back up your profiles before updating
          </span>
          <p className='font-nd-mono text-[13px] leading-relaxed text-[var(--text-primary,#e8e8e8)]'>
            A firmware update may reformat the storage partition (SPIFFS &rarr; LittleFS). When that
            happens the device starts fresh with only the <strong>Default</strong> profile and your
            saved profiles are <strong>not</strong> migrated automatically. Export your profiles to
            a file first, then re-import them after the update completes.
          </p>
          <a
            href='/profiles'
            className='nd-action-btn nd-action-btn--text inline-flex w-fit items-center gap-2'
          >
            <FontAwesomeIcon icon={faFileExport} />
            Export profiles now
          </a>
        </div>
      </div>
    </div>
  );
}

export default MigrationWarningBanner;
