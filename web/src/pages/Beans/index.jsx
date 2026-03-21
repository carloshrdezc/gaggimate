import { useCallback, useMemo, useState } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faLeaf } from '@fortawesome/free-solid-svg-icons/faLeaf';
import { BeanManagerCard } from '../ProfileList/BeanManagerCard.jsx';
import { listBeans, removeBean, saveBean } from '../../utils/beanManager.js';

const EMPTY_BEAN_DRAFT = {
  name: '',
  roaster: '',
  roastLevel: '',
  roastDate: '',
  origin: '',
  process: '',
  notes: '',
};

export function BeansPage() {
  const [beans, setBeans] = useState(() => listBeans());
  const [beanDraft, setBeanDraft] = useState(EMPTY_BEAN_DRAFT);
  const [editingBeanId, setEditingBeanId] = useState(null);

  const totalBeansLabel = useMemo(
    () => `${beans.length} bean${beans.length === 1 ? '' : 's'} saved`,
    [beans],
  );

  const resetBeanDraft = useCallback(() => {
    setBeanDraft(EMPTY_BEAN_DRAFT);
    setEditingBeanId(null);
  }, []);

  const onBeanDraftChange = useCallback((field, value) => {
    setBeanDraft(prev => ({ ...prev, [field]: value }));
  }, []);

  const onBeanSubmit = useCallback(() => {
    if (!beanDraft.name.trim()) return;
    saveBean({ ...beanDraft, id: editingBeanId || undefined });
    setBeans(listBeans());
    resetBeanDraft();
  }, [beanDraft, editingBeanId, resetBeanDraft]);

  const onBeanEdit = useCallback(bean => {
    setEditingBeanId(bean.id);
    setBeanDraft({
      name: bean.name || '',
      roaster: bean.roaster || '',
      roastLevel: bean.roastLevel || '',
      roastDate: bean.roastDate || '',
      origin: bean.origin || '',
      process: bean.process || '',
      notes: bean.notes || '',
    });
  }, []);

  const onBeanDelete = useCallback(
    beanId => {
      setBeans(removeBean(beanId));
      if (editingBeanId === beanId) {
        resetBeanDraft();
      }
    },
    [editingBeanId, resetBeanDraft],
  );

  return (
    <>
      <div className='mb-5 flex flex-col gap-3 sm:flex-row sm:items-end sm:justify-between'>
        <div>
          <div className='mb-2 inline-flex items-center gap-2 rounded-full border border-secondary/15 bg-secondary/10 px-3 py-1 text-xs font-semibold uppercase tracking-[0.24em] text-secondary'>
            <FontAwesomeIcon icon={faLeaf} />
            Bean Library
          </div>
          <h1 className='text-2xl font-bold sm:text-3xl'>Beans</h1>
          <p className='mt-2 max-w-2xl text-sm leading-relaxed text-base-content/70'>
            Manage your coffees here independently from profiles. Saved beans appear when you
            choose a profile so Shot History can capture both the profile and the bean you used.
          </p>
        </div>
        <div className='rounded-full border border-base-content/10 bg-base-100/45 px-4 py-2 text-sm font-medium text-base-content/70'>
          {totalBeansLabel}
        </div>
      </div>

      <div className='grid grid-cols-1 gap-4 lg:grid-cols-12'>
        <div className='lg:col-span-8'>
          <BeanManagerCard
            beans={beans}
            draft={beanDraft}
            editing={!!editingBeanId}
            onDraftChange={onBeanDraftChange}
            onSubmit={onBeanSubmit}
            onEdit={onBeanEdit}
            onDelete={onBeanDelete}
            onCancel={resetBeanDraft}
          />
        </div>
        <div className='lg:col-span-4'>
          <div className='rounded-[1.75rem] border border-base-content/10 bg-base-100/45 p-5 shadow-sm'>
            <h2 className='text-lg font-semibold'>Suggested Next Steps</h2>
            <ul className='mt-3 space-y-3 text-sm leading-relaxed text-base-content/70'>
              <li>Add roast date and origin so the library becomes easier to filter later.</li>
              <li>Set a default bean per profile, with the option to override before brewing.</li>
              <li>Expose bean filters in Shot History and Statistics for side-by-side comparisons.</li>
            </ul>
          </div>
        </div>
      </div>
    </>
  );
}
