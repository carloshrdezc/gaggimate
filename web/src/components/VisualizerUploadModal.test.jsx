import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => null,
}));

import VisualizerUploadModal from './VisualizerUploadModal.jsx';

const renderModal = (props = {}) =>
  render(
    h(VisualizerUploadModal, {
      isOpen: true,
      onClose: vi.fn(),
      onUpload: vi.fn().mockResolvedValue(undefined),
      ...props,
    }),
  );

beforeEach(() => {
  localStorage.clear();
});

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('VisualizerUploadModal (PRO-519)', () => {
  test('removes a legacy password on open without repopulating the password field', () => {
    localStorage.setItem('visualizer_username', 'saved-user');
    localStorage.setItem('visualizer_password', 'legacy-password');
    localStorage.setItem('visualizer_remember', 'true');

    renderModal();

    expect(screen.getByLabelText('Visualizer.coffee Username').value).toBe('saved-user');
    expect(screen.getByLabelText('Password').value).toBe('');
    expect(localStorage.getItem('visualizer_password')).toBeNull();
  });

  test('stores only the username and remember flag when opted in', async () => {
    const onUpload = vi.fn().mockResolvedValue(undefined);
    renderModal({ onUpload });

    fireEvent.change(screen.getByLabelText('Visualizer.coffee Username'), {
      target: { value: 'saved-user' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'current-password' } });
    fireEvent.click(screen.getByLabelText('Remember username on this browser'));
    fireEvent.submit(screen.getByRole('button', { name: 'Upload Shot' }).closest('form'));

    await waitFor(() => expect(onUpload).toHaveBeenCalledWith('saved-user', 'current-password', true));

    expect(localStorage.getItem('visualizer_username')).toBe('saved-user');
    expect(localStorage.getItem('visualizer_remember')).toBe('true');
    expect(localStorage.getItem('visualizer_password')).toBeNull();
  });

  test('clears remembered values and legacy passwords when opted out', async () => {
    localStorage.setItem('visualizer_username', 'saved-user');
    localStorage.setItem('visualizer_password', 'legacy-password');
    localStorage.setItem('visualizer_remember', 'true');
    const onUpload = vi.fn().mockResolvedValue(undefined);
    renderModal({ onUpload });

    fireEvent.click(screen.getByLabelText('Remember username on this browser'));
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'current-password' } });
    fireEvent.submit(screen.getByRole('button', { name: 'Upload Shot' }).closest('form'));

    await waitFor(() => expect(onUpload).toHaveBeenCalledWith('saved-user', 'current-password', false));

    expect(localStorage.getItem('visualizer_username')).toBeNull();
    expect(localStorage.getItem('visualizer_remember')).toBeNull();
    expect(localStorage.getItem('visualizer_password')).toBeNull();
  });
});
