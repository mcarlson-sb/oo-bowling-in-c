/* A client that keeps a live scoreboard: it is told each time a frame completes, with the
 * frame's number and score. */
#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "game.h"

namespace {

using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

/* The scoreboard: every completed frame it has been told about, in the order it heard. */
struct Scoreboard {
    std::vector<std::pair<int, int>> frames; /* (frame number, frame score) */
};

void Scoreboard_FrameCompleted(void *context, uint8_t frame_number, Score frame_score)
{
    auto *scoreboard = static_cast<Scoreboard *>(context);
    scoreboard->frames.emplace_back(frame_number, frame_score);
}

class ScoreboardTest : public ::testing::Test {
protected:
    GameHandle owner{Game_Create(), &Game_Destroy};
    Game *game = owner.get();
    Scoreboard scoreboard;

    void SetUp() override { Game_OnFrameCompleted(game, &Scoreboard_FrameCompleted, &scoreboard); }

    void RollAll(std::initializer_list<Pins> rolls)
    {
        for (const Pins pins : rolls) {
            EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
        }
    }
};

using Frames = std::vector<std::pair<int, int>>;

} // namespace

TEST_F(ScoreboardTest, should_tell_the_scoreboard_when_a_frame_completes)
{
    RollAll({3U, 4U});
    EXPECT_EQ((Frames{{1, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_nothing_for_a_roll_that_completes_no_frame)
{
    RollAll({3U});        /* frame 1 needs a second roll */
    RollAll({4U, 10U});   /* frame 1 completes; the strike in frame 2 doesn't */
    RollAll({2U});        /* the strike has one of its two bonus rolls */
    EXPECT_EQ((Frames{{1, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_about_two_frames_one_roll_completes_oldest_first)
{
    for (int i = 0; i < 14; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 7: gutter balls */
    }
    scoreboard.frames.clear();

    RollAll({10U, 3U}); /* frame 8: a strike; frame 9: a 3. Nothing is complete yet */
    EXPECT_EQ((Frames{}), scoreboard.frames);

    RollAll({4U}); /* the 4 is frame 8's second bonus and frame 9's second roll */
    EXPECT_EQ((Frames{{8, 17}, {9, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_the_tenth_frame_completes_only_after_its_fill_balls)
{
    for (int i = 0; i < 18; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 9: gutter balls */
    }
    scoreboard.frames.clear();

    RollAll({10U, 3U}); /* a strike and one fill ball: not complete */
    EXPECT_EQ((Frames{}), scoreboard.frames);

    RollAll({4U}); /* the second fill ball */
    EXPECT_EQ((Frames{{10, 17}}), scoreboard.frames);
}

TEST(ScoreboardNullTest, should_ignore_setting_a_callback_on_a_null_game)
{
    Scoreboard scoreboard;
    Game_OnFrameCompleted(nullptr, &Scoreboard_FrameCompleted, &scoreboard);
    EXPECT_TRUE(scoreboard.frames.empty());
}

/* ---- A second, independent subscriber --------------------------------------------------- */

namespace {

/* Running stats: how many frames are complete, and their average score. */
struct RunningStats {
    int frames = 0;
    int total = 0;
    double Average() const { return (frames == 0) ? 0.0 : static_cast<double>(total) / frames; }
};

void RunningStats_FrameCompleted(void *context, uint8_t frame_number, Score frame_score)
{
    (void)frame_number;
    auto *stats = static_cast<RunningStats *>(context);
    stats->frames++;
    stats->total += frame_score;
}

} // namespace

TEST_F(ScoreboardTest, should_tell_a_second_independent_subscriber_too)
{
    RunningStats stats;
    Game_OnFrameCompleted(game, &RunningStats_FrameCompleted, &stats);

    RollAll({3U, 4U, 10U, 5U, 5U, 1U});

    EXPECT_EQ((Frames{{1, 7}, {2, 20}, {3, 11}}), scoreboard.frames);
    EXPECT_EQ(3, stats.frames);
    EXPECT_DOUBLE_EQ(38.0 / 3.0, stats.Average());
}

TEST_F(ScoreboardTest, should_refuse_a_subscriber_once_every_slot_is_taken)
{
    RunningStats stats;
    RunningStats one_too_many;
    ASSERT_TRUE(Game_OnFrameCompleted(game, &RunningStats_FrameCompleted, &stats));
    EXPECT_FALSE(Game_OnFrameCompleted(game, &RunningStats_FrameCompleted, &one_too_many));

    RollAll({3U, 4U});
    EXPECT_EQ(1, stats.frames);
    EXPECT_EQ(0, one_too_many.frames);
}
