/* White-box tests of the Frame base class's own rules. These can't be reached through the
 * Game API: the states never break them. They are here so that a future state that does
 * break them never writes past an array. With asserts on, it stops loudly; with NDEBUG, the
 * extra roll is refused. */
#include <gtest/gtest.h>

extern "C" {
#include "regular_frame.h"
}

class FrameTest : public ::testing::Test {
protected:
    RegularFrame regular{};
    Frame *frame = RegularFrame_Init(&regular, nullptr);
};

#ifndef NDEBUG

using FrameDeathTest = FrameTest;

TEST_F(FrameDeathTest, should_stop_when_a_frame_is_given_more_rolls_than_it_holds)
{
    Frame_AddRoll(frame, 1U);
    Frame_AddRoll(frame, 1U);

    EXPECT_DEATH(Frame_AddRoll(frame, 1U), "");
}

TEST_F(FrameDeathTest, should_stop_when_a_frame_is_given_more_bonus_rolls_than_it_holds)
{
    Frame_AddBonusRoll(frame, 1U);
    Frame_AddBonusRoll(frame, 1U);

    EXPECT_DEATH(Frame_AddBonusRoll(frame, 1U), "");
}

#else

TEST_F(FrameTest, should_refuse_a_roll_past_what_a_frame_holds)
{
    Frame_AddRoll(frame, 1U);
    Frame_AddRoll(frame, 1U);
    Frame_AddRoll(frame, 7U);

    EXPECT_EQ(2U, frame->roll_count);
    EXPECT_EQ(2U, Frame_PinsKnockedDown(frame));
}

TEST_F(FrameTest, should_refuse_a_bonus_roll_past_what_a_frame_holds)
{
    Frame_AddBonusRoll(frame, 1U);
    Frame_AddBonusRoll(frame, 1U);
    Frame_AddBonusRoll(frame, 7U);

    EXPECT_EQ(2U, frame->bonus_count);
}

#endif
