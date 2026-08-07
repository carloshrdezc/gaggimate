// PRO-632: the Beanconqueror export must also be reachable from the Bean
// Library page. The Bean page has no access to shot history, so its archive is
// deliberately beans-only — the UX must say so.
import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, waitFor } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

vi.mock('../ProfileList/BeanManagerCard.jsx', () => ({
  BeanManagerCard: () => h('div', { 'data-testid': 'bean-manager-card' }),
}));

vi.mock('../../utils/beanManager.js', () => ({
  exportBeanData: vi.fn(async () => ({ beans: [] })),
  listBeans: vi.fn(async () => []),
  migrateLegacyBeansToDevice: vi.fn(async () => []),
  removeBean: vi.fn(async () => []),
  restoreBeanData: vi.fn(async () => {}),
  saveBean: vi.fn(async () => {}),
}));

vi.mock('../../utils/beanconqueror/beanconquerorDownload.js', () => ({
  downloadBeanconquerorBackup: vi.fn(async () => ({
    filename: 'Beanconqueror.zip',
    beanCount: 2,
    brewCount: 0,
  })),
}));

import { ApiServiceContext } from '../../services/ApiService.js';
import { migrateLegacyBeansToDevice } from '../../utils/beanManager.js';
import { downloadBeanconquerorBackup } from '../../utils/beanconqueror/beanconquerorDownload.js';
import { BeansPage } from './index.jsx';

const BEANS = [
  { id: 'bean-1', name: 'Pink Bourbon', archived: false },
  { id: 'bean-2', name: 'Finished Bag', archived: true },
];

function renderBeansPage(apiService = {}) {
  return render(h(ApiServiceContext.Provider, { value: apiService }, h(BeansPage, {})));
}

beforeEach(() => {
  vi.clearAllMocks();
  vi.spyOn(window, 'confirm').mockReturnValue(true);
  vi.spyOn(window, 'alert').mockImplementation(() => {});
  vi.spyOn(window, 'open').mockReturnValue(null);
  migrateLegacyBeansToDevice.mockResolvedValue(BEANS);
});

afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
});

describe('Beans page Beanconqueror export control', () => {
  test('mounts a beans-only Beanconqueror export button', async () => {
    renderBeansPage();

    const button = await screen.findByLabelText('Export Beanconqueror Backup (beans only)');
    expect(button).toBeTruthy();
  });

  test('exports the whole bean library, including archived bags, with no brews', async () => {
    renderBeansPage();
    const button = await screen.findByLabelText('Export Beanconqueror Backup (beans only)');

    fireEvent.click(button);

    await waitFor(() => {
      expect(downloadBeanconquerorBackup).toHaveBeenCalledTimes(1);
    });
    const [payload] = downloadBeanconquerorBackup.mock.calls[0];
    expect(payload.beans).toEqual(BEANS);
    expect(payload.shots).toEqual([]);
  });

  test('warns that the archive carries no shots and replaces the target library', async () => {
    renderBeansPage();
    const button = await screen.findByLabelText('Export Beanconqueror Backup (beans only)');

    fireEvent.click(button);

    await waitFor(() => {
      expect(window.confirm).toHaveBeenCalledTimes(1);
    });
    const message = window.confirm.mock.calls[0][0];
    expect(message).toMatch(/beans only/i);
    expect(message).toMatch(/no shots/i);
    expect(message).toMatch(/replaces/i);
  });

  test('does not export when the warning is declined', async () => {
    window.confirm.mockReturnValue(false);
    renderBeansPage();
    const button = await screen.findByLabelText('Export Beanconqueror Backup (beans only)');

    fireEvent.click(button);

    await waitFor(() => {
      expect(window.confirm).toHaveBeenCalledTimes(1);
    });
    expect(downloadBeanconquerorBackup).not.toHaveBeenCalled();
  });

  test('surfaces a failed export instead of failing silently', async () => {
    downloadBeanconquerorBackup.mockRejectedValueOnce(new Error('zip exploded'));
    renderBeansPage();
    const button = await screen.findByLabelText('Export Beanconqueror Backup (beans only)');

    fireEvent.click(button);

    await waitFor(() => {
      expect(window.alert).toHaveBeenCalledWith(
        expect.stringContaining('Beanconqueror export failed'),
      );
    });
  });
});
