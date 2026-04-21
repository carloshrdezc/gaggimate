import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { useCallback, useState } from 'preact/hooks';
import { Tooltip } from './Tooltip.jsx';

export function ConfirmButton({ onAction, icon, tooltip, confirmTooltip, btnSize = 'sm' }) {
  const [confirm, setConfirm] = useState(false);

  const confirmOrAction = useCallback(() => {
    if (confirm) {
      onAction();
      setConfirm(false);
    } else {
      setConfirm(true);
    }
  }, [confirm, onAction]);

  const sizeClass = btnSize === 'sm' ? 'btn-sm' : btnSize === 'lg' ? 'btn-lg' : '';

  return (
    <Tooltip content={confirm ? confirmTooltip : tooltip}>
      <button
        onClick={confirmOrAction}
        class={`btn btn-ghost ${sizeClass} transition-all duration-150 ${confirm ? 'bg-[--error] text-white font-semibold' : 'text-[--error] hover:bg-[--error]/10'}`}
        aria-label={confirm ? confirmTooltip : tooltip}
      >
        <FontAwesomeIcon icon={icon} />
        {confirm && <span class="ml-2 hidden sm:inline">Confirm</span>}
      </button>
    </Tooltip>
  );
}