const BEANS_STORAGE_KEY = 'gaggimate-beans';
const BEAN_SELECTION_EVENTS_KEY = 'gaggimate-bean-selection-events';
const ACTIVE_BEAN_SELECTION_KEY = 'gaggimate-active-bean-selection';

function normalize(text) {
  return String(text || '')
    .trim()
    .toLowerCase();
}

function readJson(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch {
    return fallback;
  }
}

function writeJson(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // ignore storage failures
  }
}

function createId(prefix) {
  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}

export function listBeans() {
  const beans = readJson(BEANS_STORAGE_KEY, []);
  return Array.isArray(beans) ? beans : [];
}

export function exportBeanData() {
  return {
    beans: listBeans(),
    selectionEvents: readJson(BEAN_SELECTION_EVENTS_KEY, []),
    activeSelection: getCurrentBeanSelection(),
  };
}

export function restoreBeanData(data) {
  writeJson(BEANS_STORAGE_KEY, Array.isArray(data?.beans) ? data.beans : []);
  writeJson(
    BEAN_SELECTION_EVENTS_KEY,
    Array.isArray(data?.selectionEvents) ? data.selectionEvents : [],
  );
  writeJson(ACTIVE_BEAN_SELECTION_KEY, data?.activeSelection || null);

  if (typeof window !== 'undefined') {
    window.dispatchEvent(
      new CustomEvent('bean-selection-changed', { detail: data?.activeSelection || null }),
    );
  }
}

export function saveBean(beanInput) {
  const beans = listBeans();
  const bean = {
    id: beanInput.id || createId('bean'),
    name: String(beanInput.name || '').trim(),
    roaster: String(beanInput.roaster || '').trim(),
    roastLevel: String(beanInput.roastLevel || '').trim(),
    roastDate: String(beanInput.roastDate || '').trim(),
    origin: String(beanInput.origin || '').trim(),
    process: String(beanInput.process || '').trim(),
    notes: String(beanInput.notes || '').trim(),
    updatedAt: Date.now(),
  };

  const nextBeans = bean.id
    ? beans.some(existing => existing.id === bean.id)
      ? beans.map(existing => (existing.id === bean.id ? { ...existing, ...bean } : existing))
      : [bean, ...beans]
    : beans;

  writeJson(BEANS_STORAGE_KEY, nextBeans);
  return bean;
}

export function removeBean(beanId) {
  const nextBeans = listBeans().filter(bean => bean.id !== beanId);
  writeJson(BEANS_STORAGE_KEY, nextBeans);
  const activeBean = getCurrentBeanSelection();
  if (activeBean?.beanId === beanId) {
    clearCurrentBeanSelection();
  }
  return nextBeans;
}

export function getLastBeanSelectionForProfile(profile) {
  const profileId = String(profile?.id || profile?.profileId || '');
  const profileName = normalize(profile?.label || profile?.name || profile?.profileLabel || '');
  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);

  return events
    .filter(event => {
      if (profileId && String(event.profileId || '') === profileId) return true;
      return profileName && normalize(event.profileLabel) === profileName;
    })
    .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0];
}

export function recordBeanSelection({ profileId, profileLabel, bean }) {
  if (!bean?.id || !bean?.name) return null;

  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);
  const nextEvent = {
    id: createId('bean-selection'),
    profileId: String(profileId || ''),
    profileLabel: String(profileLabel || ''),
    beanId: bean.id,
    beanName: bean.name,
    beanRoaster: bean.roaster || '',
    beanOrigin: bean.origin || '',
    beanProcess: bean.process || '',
    selectedAtMs: Date.now(),
  };

  const nextEvents = [nextEvent, ...events].slice(0, 500);
  writeJson(BEAN_SELECTION_EVENTS_KEY, nextEvents);
  writeJson(ACTIVE_BEAN_SELECTION_KEY, nextEvent);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('bean-selection-changed', { detail: nextEvent }));
  }
  return nextEvent;
}

export function getCurrentBeanSelection() {
  return readJson(ACTIVE_BEAN_SELECTION_KEY, null);
}

export function clearCurrentBeanSelection() {
  writeJson(ACTIVE_BEAN_SELECTION_KEY, null);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('bean-selection-changed', { detail: null }));
  }
}

export function inferBeanForShot(shot) {
  if (shot?.beanName) return shot.beanName;
  if (shot?.beanType) return shot.beanType;
  if (shot?.notes?.beanType) return shot.notes.beanType;

  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);
  const shotProfile = normalize(shot?.profile || shot?.profileName || '');
  const shotTimestampMs = Number(shot?.timestamp || 0) * 1000;

  if (!shotProfile || !Number.isFinite(shotTimestampMs) || shotTimestampMs <= 0) {
    return '';
  }

  const matchedEvent = events
    .filter(event => normalize(event.profileLabel) === shotProfile)
    .filter(event => Number(event.selectedAtMs || 0) <= shotTimestampMs)
    .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0];

  return matchedEvent?.beanName || '';
}
