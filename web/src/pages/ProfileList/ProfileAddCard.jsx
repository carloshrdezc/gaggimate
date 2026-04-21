import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';

export function ProfileAddCard() {
  return (
    <a
      href="/profiles/new"
      class="flex flex-row items-center gap-3 p-4 rounded-xl transition-all duration-150 hover:bg-[--bg-elevated] hover:border-[--border-active]"
      style="border: 1px dashed var(--border);"
      role="listitem"
    >
      <div class="flex size-10 items-center justify-center rounded-lg" style="background: var(--accent-glow);">
        <FontAwesomeIcon icon={faPlus} class="text-[--accent]" />
      </div>
      <div>
        <div class="text-sm font-medium text-[--text-primary]">Add new profile</div>
        <div class="text-xs text-[--text-muted]">Create a custom brew profile</div>
      </div>
    </a>
  );
}