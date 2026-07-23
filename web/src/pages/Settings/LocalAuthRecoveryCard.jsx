import { faCheck, faCopy, faEye, faEyeSlash } from '@fortawesome/free-solid-svg-icons';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { useContext, useState } from 'preact/hooks';
import Card from '../../components/Card.jsx';
import { ApiServiceContext } from '../../services/ApiService.js';
import {
  bootstrapLocalAuth,
  getLocalAuthToken,
  isValidLocalAuthToken,
  LOCAL_AUTH_TOKEN_ERROR,
} from '../../services/localAuthFetch.js';

// Recovery UI for browser/storage contexts that lack the local admin token
// (PRO-517 local auth). Renders unconditionally -- it must NOT depend on the
// Settings `/api/settings` useQuery succeeding, because that request 401s when
// no token is present, which is exactly the situation this card recovers from.
// Motivating case: iOS "Add to Home Screen" apps get an isolated storage sandbox
// (WebKit bug 181849) the handoff-link import path cannot reach.
export function LocalAuthRecoveryCard() {
  const apiService = useContext(ApiServiceContext);
  const [tokenInput, setTokenInput] = useState('');
  const [error, setError] = useState(null);
  const [applied, setApplied] = useState(false);
  const [currentToken, setCurrentToken] = useState(() => getLocalAuthToken());
  const [revealCurrent, setRevealCurrent] = useState(false);
  const [copied, setCopied] = useState(false);

  const onApply = () => {
    const token = tokenInput.trim();
    setApplied(false);
    if (!isValidLocalAuthToken(token)) {
      setError(LOCAL_AUTH_TOKEN_ERROR);
      return;
    }
    setError(null);
    bootstrapLocalAuth(token, apiService);
    setCurrentToken(token);
    setTokenInput('');
    setApplied(true);
  };

  const onCopyCurrent = async () => {
    if (!currentToken) return;
    setCopied(false);
    try {
      if (navigator.clipboard) {
        await navigator.clipboard.writeText(currentToken);
        setCopied(true);
      }
    } catch (err) {
      // Clipboard API needs a secure context; fall back to the visible field.
      console.error('Failed to copy local admin token:', err);
      setCopied(false);
    }
  };

  return (
    <Card sm={10} lg={10} title='Local admin token'>
      <div className='flex flex-col gap-4'>
        <div className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)]'>
          If Brew/Steam/Water or Save Settings fail because this browser has no
          saved admin token (for example an iOS home-screen app, a second device,
          or a private window), paste the device token here to sign in without
          re-running the AP setup flow.
        </div>

        <div className='flex flex-col gap-2'>
          <label
            htmlFor='localAdminTokenInput'
            className='font-nd-mono text-[14px] uppercase tracking-[0.08em] text-[var(--text-secondary,#999)]'
          >
            Paste admin token
          </label>
          <div className='flex'>
            <input
              id='localAdminTokenInput'
              name='localAdminTokenInput'
              type='text'
              autoComplete='off'
              autoCapitalize='none'
              autoCorrect='off'
              spellCheck={false}
              className='nd-input nd-input--lg flex-1'
              placeholder='32-character token'
              value={tokenInput}
              onInput={e => {
                setTokenInput(e.currentTarget.value);
                setError(null);
                setApplied(false);
              }}
              aria-describedby={error ? 'localAdminTokenError' : undefined}
              aria-invalid={error ? 'true' : undefined}
            />
            <button
              type='button'
              className='btn btn-secondary ml-2'
              onClick={onApply}
              disabled={!tokenInput.trim()}
            >
              Apply
            </button>
          </div>
          {error && (
            <div id='localAdminTokenError' role='alert' className='text-sm text-red-400'>
              {error}
            </div>
          )}
          {applied && (
            <div role='status' className='text-sm text-green-400'>
              Token applied. Try Brew/Steam/Water or Save Settings now.
            </div>
          )}
        </div>

        {currentToken && (
          <div className='border-l-2 border-[var(--text-secondary,#999)] pl-4'>
            <div className='font-nd-mono text-[13px] text-[var(--text-disabled,#666)]'>
              This session's current token -- copy it to sign in on another device
              or browser.
            </div>
            <div className='mt-2 flex items-center gap-2'>
              <div
                className='flex-1 break-all font-nd-mono text-xs select-all cursor-text rounded border border-[var(--text-secondary,#999)] p-2'
                onClick={e => {
                  const selection = window.getSelection();
                  const range = document.createRange();
                  range.selectNodeContents(e.currentTarget);
                  selection.removeAllRanges();
                  selection.addRange(range);
                }}
              >
                {revealCurrent ? currentToken : '\u2022'.repeat(currentToken.length)}
              </div>
              <button
                type='button'
                className='nd-input-unit nd-input-unit--btn'
                onClick={() => setRevealCurrent(!revealCurrent)}
                aria-label={revealCurrent ? 'Hide current token' : 'Reveal current token'}
              >
                <FontAwesomeIcon icon={revealCurrent ? faEyeSlash : faEye} />
              </button>
              <button
                type='button'
                className='btn btn-secondary'
                onClick={onCopyCurrent}
              >
                <FontAwesomeIcon icon={copied ? faCheck : faCopy} />
                <span className='ml-2'>{copied ? 'Copied' : 'Copy'}</span>
              </button>
            </div>
          </div>
        )}
      </div>
    </Card>
  );
}
