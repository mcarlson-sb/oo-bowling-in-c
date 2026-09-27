/* White-box test of the pinsetter's debug-only overlap check. It needs a hook into the
 * pinsetter, so, like frame_test.cpp, it is granted the private headers in src/. */
#include <gtest/gtest.h>

#include "pinsetter.h"
#include "pinsetter_hooks.h"

#if PINSETTER_CHECK_OVERLAP

TEST(PinsetterOverlapDeathTest, should_stop_the_program_when_two_posts_overlap)
{
    /* One producer only: a second interrupt handler, or a nested interrupt that posts while
     * another post is under way, would write the same slot. The hook leaves a post under way,
     * and the next post finds it. It all happens in the death test's child process. */
    EXPECT_DEATH(
        {
            Pinsetter *pinsetter = Pinsetter_Create();
            Pinsetter_HookPostUnderWay(pinsetter);
            (void)Pinsetter_Post(pinsetter, 3U);
        },
        "overlap");
}

#endif
