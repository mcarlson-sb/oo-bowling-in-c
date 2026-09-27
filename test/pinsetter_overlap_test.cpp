/* White-box: needs the hook in the private src/ headers. */
#include <gtest/gtest.h>

#include "pinsetter.h"
#include "pinsetter_hooks.h"

#if PINSETTER_CHECK_OVERLAP

TEST(PinsetterOverlapDeathTest, should_stop_the_program_when_two_posts_overlap)
{
    /* As a second handler, or a nested interrupt, would find it. */
    EXPECT_DEATH(
        {
            Pinsetter *pinsetter = Pinsetter_Create();
            Pinsetter_HookPostUnderWay(pinsetter);
            (void)Pinsetter_Post(pinsetter, 3U);
        },
        "overlap");
}

#endif
