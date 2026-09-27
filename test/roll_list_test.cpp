#include <gtest/gtest.h>

extern "C" {
#include "roll_list.h"
}

class RollListTest : public ::testing::Test {
protected:
    RollList list{};

    void SetUp() override { RollList_Init(&list); }
};

TEST_F(RollListTest, should_start_empty)
{
    EXPECT_EQ(0U, RollList_Count(&list));
    EXPECT_EQ(0U, RollList_Sum(&list));
}

TEST_F(RollListTest, should_keep_rolls_in_order_and_sum_them)
{
    RollList_Add(&list, 3U);
    RollList_Add(&list, 5U);

    EXPECT_EQ(2U, RollList_Count(&list));
    EXPECT_EQ(3U, RollList_At(&list, 0U));
    EXPECT_EQ(5U, RollList_At(&list, 1U));
    EXPECT_EQ(8U, RollList_Sum(&list));
}

TEST_F(RollListTest, should_be_full_at_capacity)
{
    RollList_Add(&list, 1U);
    EXPECT_FALSE(RollList_IsFull(&list));
    RollList_Add(&list, 1U);
    EXPECT_TRUE(RollList_IsFull(&list));
}

/* Debug builds stop; release refuses it. */
#ifndef NDEBUG

using RollListDeathTest = RollListTest;

TEST_F(RollListDeathTest, should_stop_on_a_roll_past_capacity)
{
    RollList_Add(&list, 1U);
    RollList_Add(&list, 1U);

    EXPECT_DEATH(RollList_Add(&list, 1U), "");
}

TEST_F(RollListDeathTest, should_stop_on_reading_a_roll_not_yet_made)
{
    RollList_Add(&list, 4U);

    EXPECT_DEATH((void)RollList_At(&list, 1U), "");
}

#else

TEST_F(RollListTest, should_refuse_a_roll_past_capacity)
{
    RollList_Add(&list, 1U);
    RollList_Add(&list, 1U);
    RollList_Add(&list, 7U);

    EXPECT_EQ(2U, RollList_Count(&list));
    EXPECT_EQ(2U, RollList_Sum(&list));
}

TEST_F(RollListTest, should_read_a_roll_not_yet_made_as_0)
{
    list.pins[1] = 7U; /* a leftover value, as real stale memory would hold */
    RollList_Add(&list, 4U);

    EXPECT_EQ(0U, RollList_At(&list, 1U));
}

#endif
