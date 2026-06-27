import { useState } from 'preact/hooks';
import { normalizeTenPointRating } from '../utils/ratings.js';

/**
 * Editable 0-10 rating number input shared by ShotHistory (ShotNotesCard) and
 * ShotAnalyzer (NotesBarExpanded).
 *
 * PRO-299: normalizing the rating on every keystroke (via
 * normalizeTenPointRating in the parent's onChange) stripped the decimal point
 * before the tenths digit could be typed — "8" then "." parsed "8." -> 8 and
 * dropped the ".", so decimals were only reachable via the spinner arrows.
 *
 * The fix: hold the user's RAW string locally while they type, and only
 * normalize + commit the numeric value to the stored notes object on blur.
 * The stored value stays a normalized number so RatingStars /
 * formatTenPointRating keep working unchanged.
 *
 * Uses `type=text` + `inputmode=decimal` rather than `type=number`: a native
 * number input's `.value` property cannot hold the trailing-dot intermediate
 * ("8." reads back as "" / "8"), so the raw string is destroyed by the DOM
 * itself before our draft state can preserve it. A text input keeps "8."
 * verbatim and inputmode=decimal still surfaces the numeric keypad on mobile.
 *
 * @param {number} value     Stored normalized rating (source of truth when not editing).
 * @param {(n:number)=>void} onCommit  Called on blur with the normalized number.
 * @param {string} className Extra classes for the <input>.
 */
export function RatingNumberInput({
  value,
  onCommit,
  className = '',
  style,
  placeholder = '0-10',
}) {
  // `null` means "not actively editing — mirror the stored value". A string
  // (including '') means the user is mid-edit and we show their raw input.
  const [draft, setDraft] = useState(null);

  const displayValue = draft !== null ? draft : value || '';

  const handleBlur = () => {
    if (draft === null) return; // never touched — nothing to commit
    onCommit(normalizeTenPointRating(draft));
    setDraft(null); // hand control back to the stored value
  };

  return (
    <input
      type='text'
      inputMode='decimal'
      className={className}
      style={style}
      value={displayValue}
      onInput={e => setDraft(e.target.value)}
      onBlur={handleBlur}
      placeholder={placeholder}
    />
  );
}
