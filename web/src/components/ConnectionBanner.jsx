import { useContext, useEffect, useState } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlugCircleXmark } from '@fortawesome/free-solid-svg-icons/faPlugCircleXmark';
import { faArrowsRotate } from '@fortawesome/free-solid-svg-icons/faArrowsRotate';
import { ApiServiceContext, connectionState, nextReconnectAt } from '../services/ApiService.js';

/**
 * Whole seconds remaining until `target` (epoch ms), clamped at 0.
 * Exported for unit testing the countdown math without a live clock.
 */
export function secondsUntil(target, now = Date.now()) {
  if (!Number.isFinite(target)) return null;
  return Math.max(0, Math.ceil((target - now) / 1000));
}

/**
 * Site-wide connection-lost banner (PRO-7).
 *
 * The web UI drives everything off a single WebSocket; when it drops (device
 * reboot, Wi-Fi hiccup) the dashboard silently stops updating. This banner
 * makes the disconnect visible: it appears within ~1s of a drop, shows a live
 * countdown to the next automatic retry, and offers a "Reconnect now" button
 * that resets the exponential backoff and retries immediately.
 *
 * Visibility is driven entirely by the `connectionState` signal — the banner
 * auto-dismisses on reconnect with no manual close. Mounted once, site-wide,
 * in the app shell so it covers every route.
 *
 * Styling mirrors MigrationWarningBanner: warning-yellow while reconnecting,
 * error-red if the connection is reported as failed.
 */
export function ConnectionBanner() {
  const apiService = useContext(ApiServiceContext);
  const state = connectionState.value;
  const target = nextReconnectAt.value;

  // Re-render ~1s so the countdown ticks down. `tick` is a throwaway counter;
  // the actual seconds value is derived from the `nextReconnectAt` timestamp,
  // so the display stays correct even if a render is skipped or delayed.
  const [, setTick] = useState(0);
  useEffect(() => {
    if (state === 'connected') return undefined;
    const id = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(id);
  }, [state]);

  if (state === 'connected') return null;

  const failed = state === 'failed';
  const seconds = failed ? null : secondsUntil(target);

  const colorVar = failed ? 'var(--color-error,#dc2626)' : 'var(--color-warning,#d4a843)';
  const bgColor = failed ? 'rgba(220,38,38,0.10)' : 'rgba(212,168,67,0.10)';

  let countdownText;
  if (failed) {
    countdownText = 'Automatic reconnection stopped.';
  } else if (seconds === null) {
    countdownText = 'Reconnecting\u2026';
  } else if (seconds <= 0) {
    countdownText = 'Reconnecting now\u2026';
  } else {
    countdownText = `Retrying in ${seconds}s\u2026`;
  }

  const onReconnect = () => {
    apiService?.reconnectNow();
  };

  return (
    <div className='mx-auto px-4 pt-3 lg:px-8 xl:container'>
      <div
        role='alert'
        aria-live='assertive'
        className='flex flex-row items-center gap-3 rounded-lg border-2 p-3'
        style={{ borderColor: colorVar, background: bgColor }}
      >
        <FontAwesomeIcon
          icon={faPlugCircleXmark}
          className='text-[18px]'
          style={{ color: colorVar }}
        />
        <div className='flex min-w-0 flex-1 flex-col gap-0.5'>
          <span
            className='font-nd-mono text-[13px] font-semibold tracking-[0.08em] uppercase'
            style={{ color: colorVar }}
          >
            Connection lost
          </span>
          <span className='font-nd-mono text-[12px] text-[var(--text-primary,#e8e8e8)]'>
            {countdownText}
          </span>
        </div>
        <button
          type='button'
          onClick={onReconnect}
          className='nd-action-btn nd-action-btn--text inline-flex w-fit shrink-0 items-center gap-2'
        >
          <FontAwesomeIcon icon={faArrowsRotate} />
          Reconnect now
        </button>
      </div>
    </div>
  );
}
