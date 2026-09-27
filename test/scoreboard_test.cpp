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

void Scoreboard_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                             bool frame_complete)
{
    (void)frame_complete; /* every message so far is a completion */
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
        ASSERT_TRUE(Game_OnFrameChanged(game, &Scoreboard_FrameChanged, &scoreboard));
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
    Game_OnFrameChanged(nullptr, &Scoreboard_FrameChanged, &scoreboard);
    EXPECT_TRUE(scoreboard.frames.empty());
}

/* ---- A second, independent subscriber --------------------------------------------------- */

/* RunningStats, the second subscriber, is in test_support.h, shared with the correction tests. */

TEST_F(ScoreboardTest, should_tell_a_second_independent_subscriber_too)
{
    RunningStats stats;
    Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats);

    RollAll(game, {3U, 4U, 10U, 5U, 5U, 1U});

    EXPECT_EQ((Frames{{1, 7}, {2, 20}, {3, 11}}), scoreboard.frames);
    EXPECT_EQ(3, stats.Frames());
    EXPECT_DOUBLE_EQ(38.0 / 3.0, stats.Average());
}

TEST_F(ScoreboardTest, should_refuse_a_subscriber_once_every_slot_is_taken)
{
    RunningStats stats;
    RunningStats one_too_many;
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
    EXPECT_FALSE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &one_too_many));

    RollAll(game, {3U, 4U});
    EXPECT_EQ(1, stats.Frames());
    EXPECT_EQ(0, one_too_many.Frames());
}

/* ---- What running stats needs under a caller's rule ------------------------------------- */

TEST(RunningStatsNoTapTest, should_average_the_counted_scores_under_no_tap)
{
    /* A league average is built from scores, and under no-tap a first-ball 9 scores as a
     * strike. So counted values, the ones the notification carries, are what stats needs. */
    GameHandle owner = MakeGameWithRule(&NinePinNoTap);
    Game *game = owner.get();
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));

    for (const Pins pins : {Pins{9U}, Pins{3U}, Pins{4U}}) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
    }

    EXPECT_EQ(2, stats.Frames());
    EXPECT_DOUBLE_EQ((17.0 + 7.0) / 2.0, stats.Average());
}

TEST_F(ScoreboardTest, should_refuse_a_null_callback_without_using_up_a_slot)
{
    EXPECT_FALSE(Game_OnFrameChanged(game, nullptr, nullptr));

    RunningStats stats; /* the fixture's scoreboard has one slot; this takes the other */
    EXPECT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
}

/* ---- Calling back into the game from inside a listener ---------------------------------- */

namespace {

/* A scoreboard that also shows the game's running score each time it hears about a frame. */
struct LiveTotal {
    Game *game = nullptr;
    std::vector<int> totals_seen;
};

void LiveTotal_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                            bool frame_complete)
{
    (void)frame_complete; /* every message so far is a completion */
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
    ASSERT_TRUE(Game_OnFrameChanged(game, &LiveTotal_FrameChanged, &live));

    for (int i = 0; i < 14; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U)); /* frames 1 to 7: gutter balls */
    }
    live.totals_seen.clear();

    RollAll(game, {10U, 3U, 4U}); /* the 4 completes frame 8 (17) and frame 9 (7) */

    /* Both notifications come after the whole roll, so both see its final score. */
    EXPECT_EQ((std::vector<int>{24, 24}), live.totals_seen);
    EXPECT_EQ(24U, Game_Score(game));
}

namespace {

/* A listener that tries to roll from inside its own notification, once. */
struct RollsFromInside {
    Game *game = nullptr;
    bool tried = false;
    GameStatus status = GAME_OK;
};

void RollsFromInside_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                  bool frame_complete)
{
    (void)frame_complete; /* every message so far is a completion */
    (void)frame_number;
    (void)frame_score;
    auto *listener = static_cast<RollsFromInside *>(context);
    if (!listener->tried) {
        listener->tried = true;
        listener->status = Game_Roll(listener->game, 4U);
    }
}

} // namespace

TEST(ListenerReentryTest, should_refuse_a_roll_made_from_inside_a_listener)
{
    /* Allowed, a roll from inside a notification completed frame 2 and told the other
     * listener about it before frame 1: out of order. Refusing it keeps the promise, the same
     * way an edit or a drain from inside a listener is refused. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollsFromInside rolls_from_inside;
    rolls_from_inside.game = game;
    Scoreboard other;
    ASSERT_TRUE(Game_OnFrameChanged(game, &RollsFromInside_FrameChanged, &rolls_from_inside));
    ASSERT_TRUE(Game_OnFrameChanged(game, &Scoreboard_FrameChanged, &other));

    RollAll(game, {5U, 5U, 3U}); /* the 3 completes frame 1, a spare (13) */

    EXPECT_EQ(GAME_ERR_DURING_NOTIFICATION, rolls_from_inside.status);
    EXPECT_EQ((Frames{{1, 13}}), other.frames); /* the refused roll changed nothing */
    EXPECT_EQ(13U, Game_Score(game));

    RollAll(game, {4U}); /* the same roll, made normally, is fine */
    EXPECT_EQ((Frames{{1, 13}, {2, 7}}), other.frames);
}
