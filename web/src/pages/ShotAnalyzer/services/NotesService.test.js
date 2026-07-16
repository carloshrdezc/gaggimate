import { beforeEach, describe, expect, it, vi } from 'vitest';

const request = vi.fn();

vi.mock('./IndexedDBService', () => ({
  indexedDBService: {},
}));

import { notesService } from './NotesService.js';

describe('NotesService.saveNotesIfMissing', () => {
  beforeEach(() => {
    request.mockReset();
    notesService.setApiService({ request });
  });

  it('uses the device atomic fill-only request rather than overwriting notes client-side', async () => {
    request.mockResolvedValue({ saved: true });

    await notesService.saveNotesIfMissing('42', 'gaggimate', { grindSetting: '3.2' });

    expect(request).toHaveBeenCalledWith({
      tp: 'req:history:notes:fill-missing',
      id: '42',
      notes: { id: '42', grindSetting: '3.2' },
    });
  });

  it('does not send a stale conditional save', async () => {
    await notesService.saveNotesIfMissing('42', 'gaggimate', { grindSetting: '3.2' }, () => false);

    expect(request).not.toHaveBeenCalled();
  });
});
