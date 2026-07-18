#include <unity.h>

#include <display/core/SettingsPersistenceTransaction.h>

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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_batch_blocks_periodic_snapshot_until_all_scalars_are_updated);
    RUN_TEST(test_write_during_flash_keeps_dirty_for_follow_up_snapshot);
    return UNITY_END();
}
