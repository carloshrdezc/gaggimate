#include <unity.h>

#include <display/core/SettingsPersistenceMutexInitialization.h>
#include <display/core/SettingsPersistenceTransaction.h>

namespace {
int createdMutex;
int existingMutex;
int createCalls;

int *createMutex() {
    ++createCalls;
    return &createdMutex;
}
} // namespace

void test_persistence_mutex_initialization_creates_missing_mutex_once() {
    createCalls = 0;

    int *mutex = initializePersistenceMutex<int *>(nullptr, createMutex);

    TEST_ASSERT_EQUAL_PTR(&createdMutex, mutex);
    TEST_ASSERT_EQUAL_UINT(1, createCalls);
}

void test_persistence_mutex_initialization_preserves_existing_mutex() {
    createCalls = 0;

    int *mutex = initializePersistenceMutex<int *>(&existingMutex, createMutex);

    TEST_ASSERT_EQUAL_PTR(&existingMutex, mutex);
    TEST_ASSERT_EQUAL_UINT(0, createCalls);
}

void test_batch_blocks_periodic_snapshot_until_all_scalars_are_updated() {
    SettingsPersistenceTransaction transaction;
    transaction.beginBatch();
    transaction.markDirty();
    TEST_ASSERT_TRUE(transaction.isDirty());
    TEST_ASSERT_FALSE(transaction.tryBeginSnapshot());
    transaction.endBatch();
    TEST_ASSERT_TRUE(transaction.tryBeginSnapshot());
    TEST_ASSERT_FALSE(transaction.isDirty());
}

void test_write_during_flash_keeps_dirty_for_follow_up_snapshot() {
    SettingsPersistenceTransaction transaction;
    transaction.markDirty();
    TEST_ASSERT_TRUE(transaction.tryBeginSnapshot());
    transaction.markDirty();
    TEST_ASSERT_TRUE(transaction.isDirty());
}

void test_immediate_save_requested_during_batch_is_deferred_until_batch_ends() {
    SettingsPersistenceTransaction transaction;
    transaction.beginBatch();
    transaction.requestImmediateSave();
    TEST_ASSERT_FALSE(transaction.consumeImmediateSaveRequest());
    transaction.endBatch();
    TEST_ASSERT_TRUE(transaction.consumeImmediateSaveRequest());
    TEST_ASSERT_FALSE(transaction.consumeImmediateSaveRequest());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_persistence_mutex_initialization_creates_missing_mutex_once);
    RUN_TEST(test_persistence_mutex_initialization_preserves_existing_mutex);
    RUN_TEST(test_batch_blocks_periodic_snapshot_until_all_scalars_are_updated);
    RUN_TEST(test_write_during_flash_keeps_dirty_for_follow_up_snapshot);
    RUN_TEST(test_immediate_save_requested_during_batch_is_deferred_until_batch_ends);
    return UNITY_END();
}
