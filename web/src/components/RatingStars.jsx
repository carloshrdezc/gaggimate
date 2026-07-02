import { getRatingFillPercent } from '../utils/ratings.js';

const STARS = '\u2605\u2605\u2605\u2605\u2605';

/**
 * Read-only proportional 5-star rating display.
 *
 * Renders a base row of empty stars with a clipped, overlaid filled row whose
 * width is driven by getRatingFillPercent(rating) on the 0-10 scale. Shared by
 * ShotHistory (ShotNotesCard) and ShotAnalyzer (NotesBarExpanded) so the star
 * markup and the empty-star color token live in exactly one place (PRO-300).
 *
 * The empty-star color uses the theme-aware daisyUI `text-base-content/20`
 * token so it tracks the active theme instead of a hard-coded gray.
 */
export function RatingStars({ rating, className = '' }) {
  return (
    <div className={`relative inline-flex text-lg leading-none ${className}`.trim()}>
      <div className='text-base-content/20'>{STARS}</div>
      <div
        className='absolute inset-y-0 left-0 overflow-hidden whitespace-nowrap text-yellow-400'
        style={{ width: getRatingFillPercent(rating) }}
      >
        {STARS}
      </div>
    </div>
  );
}
