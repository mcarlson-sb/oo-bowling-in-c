#include <gtest/gtest.h>

extern "C" {
#include "frame_listeners.h"
}

namespace {

void Counts_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                         bool frame_complete)
{
    (void)frame_number;
    (void)frame_score;
    (void)frame_complete;
    ++*static_cast<int *>(context);
}

} // namespace

class FrameListenersTest : public ::testing::Test {
protected:
    void SetUp() override { FrameListeners_Init(&listeners); }

    FrameListeners listeners;
};

TEST_F(FrameListenersTest, should_tell_only_the_newest_listener)
{
    int first = 0;
    int newest = 0;
    ASSERT_TRUE(FrameListeners_Add(&listeners, &Counts_FrameChanged, &first));
    ASSERT_TRUE(FrameListeners_Add(&listeners, &Counts_FrameChanged, &newest));

    FrameListeners_TellNewest(&listeners, 1U, 10U, true);

    EXPECT_EQ(0, first);
    EXPECT_EQ(1, newest);
}

/* The game only catches up a listener it has just added. Debug builds stop; release tells no
 * one. */
#ifndef NDEBUG

using FrameListenersDeathTest = FrameListenersTest;

TEST_F(FrameListenersDeathTest, should_stop_on_telling_the_newest_of_no_listeners)
{
    EXPECT_DEATH(FrameListeners_TellNewest(&listeners, 1U, 10U, true), "");
}

#else

TEST_F(FrameListenersTest, should_tell_no_one_when_telling_the_newest_of_no_listeners)
{
    FrameListeners_TellNewest(&listeners, 1U, 10U, true);
}

#endif
