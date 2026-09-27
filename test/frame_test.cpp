/* White-box: the Game API always keeps Frame's contract, so these break it directly. */
#include <gtest/gtest.h>

extern "C" {
#include "regular_frame.h"
}

#ifndef NDEBUG

TEST(FrameDeathTest, should_stop_a_roll_made_without_a_context)
{
    RegularFrame regular{};
    Frame *frame = RegularFrame_Init(&regular);

    EXPECT_DEATH((void)Frame_Roll(frame, nullptr, 3U), "");
}

#endif
