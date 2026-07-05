import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { useRef } from 'preact/hooks';

/**
 * Shared file-import control: a button that triggers a hidden <input type="file">
 * via a ref (importInputRef.current?.click()). This is the robust idiom — it does
 * NOT rely on a <label htmlFor> / <input id> pairing, which silently breaks when
 * the id and htmlFor drift apart (the bug class PRO-395 and PRO-399 had to repair).
 *
 * @param {object} props
 * @param {(event: Event) => void} props.onChange - change handler for the file input
 * @param {string} [props.title='Import'] - button title + accessible name
 * @param {string} [props.accept='.json,application/json'] - accepted file types
 * @param {boolean} [props.multiple=false] - allow selecting multiple files
 * @param {boolean} [props.disabled=false] - disable the trigger button
 * @param {string} [props.className='nd-action-btn'] - button styling classes
 * @param {import('@fortawesome/fontawesome-svg-core').IconDefinition} [props.icon] - button icon
 */
export function ImportButton({
  onChange,
  title = 'Import',
  accept = '.json,application/json',
  multiple = false,
  disabled = false,
  className = 'nd-action-btn',
  icon = faFileImport,
}) {
  const importInputRef = useRef(null);

  return (
    <>
      <button
        type='button'
        className={className}
        onClick={() => importInputRef.current?.click()}
        disabled={disabled}
        title={title}
        aria-label={title}
      >
        <FontAwesomeIcon icon={icon} />
      </button>
      <input
        ref={importInputRef}
        type='file'
        className='hidden'
        accept={accept}
        multiple={multiple}
        onChange={onChange}
      />
    </>
  );
}
