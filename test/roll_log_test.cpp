#include <gtest/gtest.h>

extern "C" {
#include "roll_log.h"
}

class RollLogTest : public ::testing::Test {
protected:
    RollLog log{};

    void SetUp() override { RollLog_Init(&log); }

    void Fill()
    {
        for (uint8_t i = 0U; i < GAME_MAX_ROLLS; i++) {
            RollLog_Append(&log, 1U);
        }
    }
};

TEST_F(RollLogTest, should_keep_rolls_in_order)
{
    RollLog_Append(&log, 3U);
    RollLog_Append(&log, 5U);

    EXPECT_EQ(2U, RollLog_Count(&log));
    EXPECT_EQ(3U, RollLog_At(&log, 0U));
    EXPECT_EQ(5U, RollLog_At(&log, 1U));
}

/* Debug builds stop; release refuses the write, or reads 0. */
#ifndef NDEBUG

using RollLogDeathTest = RollLogTest;

TEST_F(RollLogDeathTest, should_stop_on_a_roll_past_a_whole_game)
{
    Fill();

    EXPECT_DEATH(RollLog_Append(&log, 1U), "");
}

TEST_F(RollLogDeathTest, should_stop_on_reading_a_roll_not_yet_made)
{
    RollLog_Append(&log, 4U);

    EXPECT_DEATH((void)RollLog_At(&log, 1U), "");
}

#else

TEST_F(RollLogTest, should_refuse_a_roll_past_a_whole_game)
{
    Fill();
    RollLog_Append(&log, 7U);

    EXPECT_EQ(GAME_MAX_ROLLS, RollLog_Count(&log));
    EXPECT_EQ(1U, RollLog_At(&log, static_cast<uint8_t>(GAME_MAX_ROLLS - 1U)));
}

TEST_F(RollLogTest, should_read_a_roll_not_yet_made_as_0)
{
    log.pins[1] = 7U; /* a leftover value, as real stale memory would hold */
    RollLog_Append(&log, 4U);

    EXPECT_EQ(0U, RollLog_At(&log, 1U));
}

#endif
