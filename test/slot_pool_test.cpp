#include <gtest/gtest.h>

extern "C" {
#include "slot_pool.h"
}

struct Thing {
    int a;
    int b;
};

class SlotPoolTest : public ::testing::Test {
protected:
    Thing things[2] = {};
    bool in_use[2] = {false, false};
    SlotPool pool = {in_use, 2U, things, sizeof(Thing)};
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
    SlotPool small = {guard, 2U, things, sizeof(Thing)};

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

TEST_F(SlotPoolTest, should_find_the_slot_each_object_is_in)
{
    EXPECT_TRUE(SlotPool_Find(&pool, &things[0], &slot));
    EXPECT_EQ(0U, slot);
    EXPECT_TRUE(SlotPool_Find(&pool, &things[1], &slot));
    EXPECT_EQ(1U, slot);
}

TEST_F(SlotPoolTest, should_not_find_null_or_an_object_outside_the_pool)
{
    Thing elsewhere = {};

    EXPECT_FALSE(SlotPool_Find(&pool, nullptr, &slot));
    EXPECT_FALSE(SlotPool_Find(&pool, &elsewhere, &slot));
    EXPECT_EQ(0xFFU, slot);
}

TEST_F(SlotPoolTest, should_not_find_an_address_inside_an_object)
{
    EXPECT_FALSE(SlotPool_Find(&pool, &things[1].b, &slot));
    EXPECT_EQ(0xFFU, slot);
}

TEST_F(SlotPoolTest, should_take_the_first_object_of_an_empty_pool)
{
    EXPECT_EQ(&things[0], SlotPool_Take(&pool));
}

TEST_F(SlotPoolTest, should_take_each_object_once_and_then_none)
{
    EXPECT_EQ(&things[0], SlotPool_Take(&pool));
    EXPECT_EQ(&things[1], SlotPool_Take(&pool));
    EXPECT_EQ(nullptr, SlotPool_Take(&pool));
}

TEST_F(SlotPoolTest, should_hold_its_objects_and_nothing_else)
{
    Thing elsewhere = {};

    EXPECT_TRUE(SlotPool_Holds(&pool, &things[0]));
    EXPECT_TRUE(SlotPool_Holds(&pool, &things[1]));
    EXPECT_FALSE(SlotPool_Holds(&pool, nullptr));
    EXPECT_FALSE(SlotPool_Holds(&pool, &elsewhere));
    EXPECT_FALSE(SlotPool_Holds(&pool, &things[1].b));
}

TEST_F(SlotPoolTest, should_report_whether_a_returned_object_was_taken)
{
    Thing elsewhere = {};
    ASSERT_EQ(&things[0], SlotPool_Take(&pool));

    EXPECT_TRUE(SlotPool_Return(&pool, &things[0]));
    EXPECT_FALSE(SlotPool_Return(&pool, &things[0])); /* already returned */
    EXPECT_FALSE(SlotPool_Return(&pool, &elsewhere)); /* not one of the pool's */
}

TEST_F(SlotPoolTest, should_take_a_returned_object_again)
{
    ASSERT_EQ(&things[0], SlotPool_Take(&pool));
    ASSERT_EQ(&things[1], SlotPool_Take(&pool));
    ASSERT_TRUE(SlotPool_Return(&pool, &things[0]));

    EXPECT_EQ(&things[0], SlotPool_Take(&pool));
}
