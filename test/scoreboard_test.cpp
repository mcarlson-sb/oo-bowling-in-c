/* A client that keeps a live scoreboard: it is told each time a frame completes, with the
 * frame's number and score. */
#include <utility>
#include <vector>

#include "test_support.h"

namespace {

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
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    Scoreboard scoreboard;

    void SetUp() override
    {
        ASSERT_TRUE(Game_OnFrameCompleted(game, &Scoreboard_FrameCompleted, &scoreboard));
    }
};

using Frames = std::vector<std::pair<int, int>>;

} // namespace

TEST_F(ScoreboardTest, should_tell_the_scoreboard_when_a_frame_completes)
{
    RollAll(game, {3U, 4U});
    EXPECT_EQ((Frames{{1, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_nothing_for_a_roll_that_completes_no_frame)
{
    RollAll(game, {3U});        /* frame 1 needs a second roll */
    RollAll(game, {4U, 10U});   /* frame 1 completes; the strike in frame 2 doesn't */
    RollAll(game, {2U});        /* the strike has one of its two bonus rolls */
    EXPECT_EQ((Frames{{1, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_about_two_frames_one_roll_completes_oldest_first)
{
    for (int i = 0; i < 14; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 7: gutter balls */
    }
    scoreboard.frames.clear();

    RollAll(game, {10U, 3U}); /* frame 8: a strike; frame 9: a 3. Nothing is complete yet */
    EXPECT_EQ((Frames{}), scoreboard.frames);

    RollAll(game, {4U}); /* the 4 is frame 8's second bonus and frame 9's second roll */
    EXPECT_EQ((Frames{{8, 17}, {9, 7}}), scoreboard.frames);
}

TEST_F(ScoreboardTest, should_tell_the_scoreboard_about_the_tenth_frame_only_after_its_fill_balls)
{
    for (int i = 0; i < 18; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 9: gutter balls */
    }
    scoreboard.frames.clear();

    RollAll(game, {10U, 3U}); /* a strike and one fill ball: not complete */
    EXPECT_EQ((Frames{}), scoreboard.frames);

    RollAll(game, {4U}); /* the second fill ball */
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

    RollAll(game, {3U, 4U, 10U, 5U, 5U, 1U});

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

    RollAll(game, {3U, 4U});
    EXPECT_EQ(1, stats.frames);
    EXPECT_EQ(0, one_too_many.frames);
}

/* ---- What running stats needs under a caller's rule ------------------------------------- */

TEST(RunningStatsNoTapTest, should_average_the_counted_scores_under_no_tap)
{
    /* A league average is built from scores, and under no-tap a first-ball 9 scores as a
     * strike. So counted values, the ones the notification carries, are what stats needs. */
    GameHandle owner = MakeGameWithRule(&NinePinNoTap);
    Game *game = owner.get();
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameCompleted(game, &RunningStats_FrameCompleted, &stats));

    for (const Pins pins : {Pins{9U}, Pins{3U}, Pins{4U}}) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
    }

    EXPECT_EQ(2, stats.frames);
    EXPECT_DOUBLE_EQ((17.0 + 7.0) / 2.0, stats.Average());
}

TEST_F(ScoreboardTest, should_refuse_a_null_callback_without_using_up_a_slot)
{
    EXPECT_FALSE(Game_OnFrameCompleted(game, nullptr, nullptr));

    RunningStats stats; /* the fixture's scoreboard has one slot; this takes the other */
    EXPECT_TRUE(Game_OnFrameCompleted(game, &RunningStats_FrameCompleted, &stats));
}

/* ---- Calling back into the game from inside a listener ---------------------------------- */

namespace {

/* A scoreboard that also shows the game's running score each time it hears about a frame. */
struct LiveTotal {
    Game *game = nullptr;
    std::vector<int> totals_seen;
};

void LiveTotal_FrameCompleted(void *context, uint8_t frame_number, Score frame_score)
{
    (void)frame_number;
    (void)frame_score;
    auto *live = static_cast<LiveTotal *>(context);
    live->totals_seen.push_back(Game_Score(live->game));
}

} // namespace

TEST(ListenerReentryTest, should_let_a_listener_read_the_score_of_the_whole_roll)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    LiveTotal live;
    live.game = game;
    ASSERT_TRUE(Game_OnFrameCompleted(game, &LiveTotal_FrameCompleted, &live));

    for (int i = 0; i < 14; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 7: gutter balls */
    }
    live.totals_seen.clear();

    RollAll(game, {10U, 3U, 4U}); /* the 4 completes frame 8 (17) and frame 9 (7) */

    /* Both notifications come after the whole roll, so both see its final score. */
    EXPECT_EQ((std::vector<int>{24, 24}), live.totals_seen);
    EXPECT_EQ(24U, Game_Score(game));
}
