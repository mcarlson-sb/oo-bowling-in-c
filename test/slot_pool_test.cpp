#include <gtest/gtest.h>

extern "C" {
#include "slot_pool.h"
}

class SlotPoolTest : public ::testing::Test {
protected:
    bool in_use[2] = {false, false};
    SlotPool pool = {in_use, 2U};
    uint8_t slot = 0xFFU;
};

TEST_F(SlotPoolTest, should_hand_out_the_first_slot_of_an_empty_pool)
{
    EXPECT_TRUE(SlotPool_Acquire(&pool, &slot));
    EXPECT_EQ(0U, slot);
}

TEST_F(SlotPoolTest, should_hand_out_a_different_slot_each_time)
{
    uint8_t second = 0xFFU;
    ASSERT_TRUE(SlotPool_Acquire(&pool, &slot));
    ASSERT_TRUE(SlotPool_Acquire(&pool, &second));
    EXPECT_NE(slot, second);
}

TEST_F(SlotPoolTest, should_refuse_when_every_slot_is_in_use)
{
    ASSERT_TRUE(SlotPool_Acquire(&pool, &slot));
    ASSERT_TRUE(SlotPool_Acquire(&pool, &slot));

    slot = 0xFFU;
    EXPECT_FALSE(SlotPool_Acquire(&pool, &slot));
    EXPECT_EQ(0xFFU, slot);
}

TEST_F(SlotPoolTest, should_hand_out_a_released_slot_again)
{
    uint8_t second = 0xFFU;
    ASSERT_TRUE(SlotPool_Acquire(&pool, &slot));
    ASSERT_TRUE(SlotPool_Acquire(&pool, &second));

    SlotPool_Release(&pool, slot);
    uint8_t reused = 0xFFU;
    EXPECT_TRUE(SlotPool_Acquire(&pool, &reused));
    EXPECT_EQ(slot, reused);
}

TEST_F(SlotPoolTest, should_ignore_releasing_a_slot_it_does_not_have)
{
    bool guard[3] = {false, false, false}; /* in_use, plus one flag just past its end */
    SlotPool small = {guard, 2U};

    guard[2] = true;
    SlotPool_Release(&small, 2U);

    EXPECT_TRUE(guard[2]); /* nothing past the pool was written */
}

TEST_F(SlotPoolTest, should_report_whether_a_released_slot_was_in_use)
{
    ASSERT_TRUE(SlotPool_Acquire(&pool, &slot));

    EXPECT_TRUE(SlotPool_Release(&pool, slot));
    EXPECT_FALSE(SlotPool_Release(&pool, slot)); /* already free */
    EXPECT_FALSE(SlotPool_Release(&pool, 7U));   /* not a slot of this pool */
}
