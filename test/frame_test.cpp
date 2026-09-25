/* White-box tests of the Frame base class's contract checks. These can't be reached through
 * the Game API, which always keeps the contract; they make sure a caller that breaks it is
 * stopped in a debug build. */
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
