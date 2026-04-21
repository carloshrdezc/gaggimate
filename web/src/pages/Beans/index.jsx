import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { ApiServiceContext } from '../../services/ApiService.js';
import { downloadJson, prepareDownload } from '../../utils/download.js';
import {
  exportBeanData,
  listBeans,
  migrateLegacyBeansToDevice,
  removeBean,
  restoreBeanData,
  saveBean,
} from '../../utils/beanManager.js';
import { BeanManagerCard } from '../ProfileList/BeanManagerCard.jsx';

const EMPTY_BEAN_DRAFT = {
  name: '',
  roaster: '',
  roastLevel: '',
  roastDate: '',
  origin: '',
  process: '',
  quantity: '',
  notes: '',
  archived: false,
};

export function BeansPage() {
  const apiService = useContext(ApiServiceContext);
  const importInputRef = useRef(null);
  const [beans, setBeans] = useState([]);
  const [beanDraft, setBeanDraft] = useState(EMPTY_BEAN_DRAFT);
  const [editingBeanId, setEditingBeanId] = useState(null);
  const [showArchived, setShowArchived] = useState(false);
  const [busy, setBusy] = useState(false);

  const loadBeans = useCallback(async () => {
    const loadedBeans = await listBeans(apiService);
    setBeans(loadedBeans);
    return loadedBeans;
  }, [apiService]);

  useEffect(() => {
    let cancelled = false;
    const hydrate = async () => {
      try {
        setBusy(true);
        await migrateLegacyBeansToDevice(apiService);
        const loadedBeans = await listBeans(apiService);
        if (!cancelled) setBeans(loadedBeans);
      } catch (error) {
        console.error('Failed to load beans:', error);
      } finally {
        if (!cancelled) setBusy(false);
      }
    };
    hydrate();
    const handleBeansChanged = () => loadBeans().catch(error => console.error('Failed to refresh beans:', error));
    window.addEventListener('beans-library-changed', handleBeansChanged);
    return () => {
      cancelled = true;
      window.removeEventListener('beans-library-changed', handleBeansChanged);
    };
  }, [apiService, loadBeans]);

  const visibleBeans = useMemo(
    () => beans.filter(bean => (showArchived ? true : !bean.archived)),
    [beans, showArchived],
  );

  const activeCount = useMemo(() => beans.filter(bean => !bean.archived).length, [beans]);
  const archivedCount = useMemo(() => beans.filter(bean => bean.archived).length, [beans]);
  const totalBeansLabel = useMemo(
    () => `${activeCount} active${archivedCount ? ` · ${archivedCount} archived` : ''}`,
    [activeCount, archivedCount],
  );

  const resetBeanDraft = useCallback(() => {
    setBeanDraft(EMPTY_BEAN_DRAFT);
    setEditingBeanId(null);
  }, []);

  const onBeanDraftChange = useCallback((field, value) => {
    setBeanDraft(prev => ({ ...prev, [field]: value }));
  }, []);

  const onBeanSubmit = useCallback(async () => {
    if (!beanDraft.name.trim()) return;
    setBusy(true);
    try {
      await saveBean(apiService, { ...beanDraft, id: editingBeanId || undefined });
      await loadBeans();
      resetBeanDraft();
    } finally {
      setBusy(false);
    }
  }, [apiService, beanDraft, editingBeanId, loadBeans, resetBeanDraft]);

  const onBeanEdit = useCallback(bean => {
    setEditingBeanId(bean.id);
    setBeanDraft({
      name: bean.name || '',
      roaster: bean.roaster || '',
      roastLevel: bean.roastLevel || '',
      roastDate: bean.roastDate || '',
      origin: bean.origin || '',
      process: bean.process || '',
      quantity: bean.quantity ?? '',
      notes: bean.notes || '',
      archived: !!bean.archived,
    });
  }, []);

  const onBeanDelete = useCallback(
    async beanId => {
      setBusy(true);
      try {
        setBeans(await removeBean(apiService, beanId));
        if (editingBeanId === beanId) resetBeanDraft();
      } finally {
        setBusy(false);
      }
    },
    [apiService, editingBeanId, resetBeanDraft],
  );

  const onBeanArchiveToggle = useCallback(
    async bean => {
      setBusy(true);
      try {
        await saveBean(apiService, { ...bean, archived: !bean.archived });
        await loadBeans();
        if (editingBeanId === bean.id) {
          setBeanDraft(prev => ({ ...prev, archived: !bean.archived }));
        }
      } finally {
        setBusy(false);
      }
    },
    [apiService, editingBeanId, loadBeans],
  );

  const onExport = useCallback(async () => {
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    const filename = `beans-${stamp}.json`;
    const download = prepareDownload(filename);
    setBusy(true);
    try {
      const archive = await exportBeanData(apiService);
      downloadJson(archive, filename, download);
    } catch (error) {
      console.error('Failed to export beans:', error);
      download.fail(error);
      alert(`Bean export failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }, [apiService]);

  const onImport = useCallback(
    async event => {
      const [file] = Array.from(event.target.files || []);
      if (!file) return;
      setBusy(true);
      try {
        const payload = JSON.parse(await file.text());
        await restoreBeanData(apiService, payload);
        await loadBeans();
        alert('Bean backup imported successfully.');
      } catch (error) {
        console.error('Failed to import bean backup:', error);
        alert(`Bean import failed: ${error.message}`);
      } finally {
        event.target.value = '';
        setBusy(false);
      }
    },
    [apiService, loadBeans],
  );

  // Loading skeleton
  if (busy && beans.length === 0) {
    return (
      <div class="space-y-4">
        <div class="h-8 w-24 rounded-lg skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
        <div class="h-48 rounded-xl skeleton" style="background: linear-gradient(90deg, var(--bg-elevated) 0%, var(--border) 50%, var(--bg-elevated) 100%); background-size: 200% 100%; animation: shimmer 1.5s infinite;" />
      </div>
    );
  }

  return (
    <div class="space-y-6">
      {/* Page Header */}
      <div class="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
        <div class="flex-1">
          <div class="mb-2 inline-flex items-center gap-2 rounded-full px-3 py-1 text-xs font-semibold uppercase tracking-widest" style="color: var(--accent); background: var(--accent-glow);">
            <FontAwesomeIcon icon={faLeaf} />
            Bean Library
          </div>
          <h1 class="text-2xl font-semibold text-[--text-primary]">Beans</h1>
          <p class="mt-2 max-w-xl text-sm leading-relaxed text-[--text-secondary]">
            Beans are stored on the machine — the same library is available from any device. Track remaining quantity, archive finished bags, and export a backup.
          </p>
        </div>
        <div class="flex flex-col items-end gap-3">
          <div class="text-sm text-[--text-muted]">{totalBeansLabel}</div>
          <div class="flex flex-wrap gap-2">
            <button
              onClick={onExport}
              disabled={busy || beans.length === 0}
              class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all disabled:opacity-50"
            >
              <FontAwesomeIcon icon={faFileExport} />
              <span>Export</span>
            </button>
            <button
              onClick={() => importInputRef.current?.click()}
              disabled={busy}
              class="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-[--text-secondary] border border-[--border] hover:border-[--border-active] hover:text-[--text-primary] transition-all"
            >
              <FontAwesomeIcon icon={faFileImport} />
              <span>Import</span>
            </button>
            <input
              ref={importInputRef}
              type="file"
              accept=".json,application/json"
              class="hidden"
              onChange={onImport}
            />
          </div>
        </div>
      </div>

      {/* Active / All toggle */}
      <div class="flex gap-1 p-1 rounded-lg w-fit" style="background: var(--bg-elevated);">
        <button
          type="button"
          onClick={() => setShowArchived(false)}
          class="px-4 py-1.5 rounded-md text-sm font-medium transition-all"
          style={!showArchived ? 'background: var(--accent); color: var(--bg-base);' : 'color: var(--text-secondary);'}
        >
          Active Beans
        </button>
        <button
          type="button"
          onClick={() => setShowArchived(true)}
          class="px-4 py-1.5 rounded-md text-sm font-medium transition-all"
          style={showArchived ? 'background: var(--accent); color: var(--bg-base);' : 'color: var(--text-secondary);'}
        >
          All Beans
        </button>
      </div>

      {/* Bean form + list */}
      <BeanManagerCard
        beans={visibleBeans}
        draft={beanDraft}
        editing={!!editingBeanId}
        onDraftChange={onBeanDraftChange}
        onSubmit={onBeanSubmit}
        onEdit={onBeanEdit}
        onDelete={onBeanDelete}
        onArchiveToggle={onBeanArchiveToggle}
        onCancel={resetBeanDraft}
        busy={busy}
      />
    </div>
  );
}