/* White-box tests of the Frame base class's own rules. These can't be reached through the
 * Game API: the states never break them. The checks are here so that a future state that
 * does break them stops loudly, not by silently writing past an array. */
#include <gtest/gtest.h>

extern "C" {
#include "regular_frame.h"
}

TEST(FrameDeathTest, should_stop_when_a_frame_is_given_more_rolls_than_it_holds)
{
    RegularFrame regular;
    Frame *frame = RegularFrame_Init(&regular, nullptr);
    Frame_AddRoll(frame, 1U);
    Frame_AddRoll(frame, 1U);

    EXPECT_DEATH(Frame_AddRoll(frame, 1U), "");
}
