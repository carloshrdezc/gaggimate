#pragma once

class SettingsPersistenceTransaction {
  public:
    void beginBatch() { ++batchDepth; }

    void endBatch() {
        if (batchDepth > 0) {
            --batchDepth;
        }
    }

    void markDirty() { dirty = true; }

    void clearDirty() { dirty = false; }

    void requestImmediateSave() { immediateSaveRequested = true; }

    bool consumeImmediateSaveRequest() {
        if (batchDepth > 0 || !immediateSaveRequested) {
            return false;
        }
        immediateSaveRequested = false;
        return true;
    }

    bool tryBeginSnapshot() {
        if (batchDepth > 0 || !dirty) {
            return false;
        }
        dirty = false;
        return true;
    }

    bool isDirty() const { return dirty; }

  private:
    unsigned int batchDepth = 0;
    bool dirty = false;
    bool immediateSaveRequested = false;
};
