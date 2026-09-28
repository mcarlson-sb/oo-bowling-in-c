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
};

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

TEST_F(SlotPoolTest, should_write_nothing_when_returning_an_object_it_does_not_hold)
{
    bool guard[3] = {false, false, false}; /* in_use, plus one flag just past its end */
    SlotPool small = {guard, 2U, things, sizeof(Thing)};
    Thing elsewhere = {};

    guard[2] = true;
    EXPECT_FALSE(SlotPool_Return(&small, &elsewhere));

    EXPECT_TRUE(guard[2]);
    EXPECT_FALSE(guard[0]);
    EXPECT_FALSE(guard[1]);
}
