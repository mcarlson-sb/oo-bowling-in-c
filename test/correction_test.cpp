/* A scorer fixing a roll entered wrongly: the game rescores everything after it. */
#include <map>
#include <random>
#include <utility>
#include <vector>

#include "test_support.h"

TEST(CorrectionTest, should_rescore_the_game_when_a_roll_is_corrected)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* the first roll was a 5, not a 3 */
    EXPECT_EQ(9U, Game_Score(game));
}

TEST(CorrectionTest, should_reject_a_correction_that_makes_a_later_roll_impossible)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {7U, 3U, 5U}); /* a spare, 7 + 3, then 5: 15 so far */

    /* Correcting the 7 to an 8 would make the 3 too many pins (8 + 3 > 10). */
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_CorrectRoll(game, 1U, 8U));
    EXPECT_EQ(15U, Game_Score(game)); /* unchanged */

    RollAll(game, {4U}); /* and the game carries on from where it was */
    EXPECT_EQ(24U, Game_Score(game));
}

TEST(CorrectionTest, should_recount_every_replayed_roll_from_the_pins_that_fell)
{
    /* Predicted in phase 1, two phases before a feature needed it: replaying means applying
     * the caller's rule again, so the game must keep the pins that fell, not what they were
     * counted as. */
    GameHandle owner = MakeGameWithRule(&NinePinNoTap);
    Game *game = owner.get();
    RollAll(game, {10U, 9U, 3U}); /* a strike, then a no-tap 9 counted as a strike, then 3 */

    /* The first roll was really a 1. Now the 9 is a second ball, at 9 pins standing: a
     * spare, not a strike. It must be counted again from the 9 that fell. */
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 1U));
    EXPECT_EQ(13U, Game_Score(game)); /* spare 1 + 9, plus its bonus 3 */
}

/* ---- Telling the listeners about a correction ------------------------------------------- *
 * A correction is told with the same frame-completed message, and a listener treats a frame
 * number it has heard before as an update. */


namespace {

/* A scoreboard that keeps the latest score it has heard for each frame. */
struct KeyedScoreboard {
    std::map<int, int> scores; /* frame number -> score */
};

void KeyedScoreboard_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                  bool frame_complete)
{
    auto *scoreboard = static_cast<KeyedScoreboard *>(context);
    if (frame_complete) {
        scoreboard->scores[frame_number] = frame_score;
    } else {
        scoreboard->scores.erase(frame_number); /* reopened by a correction */
    }
}

} // namespace

TEST(CorrectionListenerTest, should_tell_the_scoreboard_the_rescored_frames)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    KeyedScoreboard scoreboard;
    ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));
    RollAll(game, {3U, 4U, 5U, 2U});
    ASSERT_EQ((std::map<int, int>{{1, 7}, {2, 7}}), scoreboard.scores);

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* frame 1 was 5, 4 */
    EXPECT_EQ((std::map<int, int>{{1, 9}, {2, 7}}), scoreboard.scores);
}

TEST(CorrectionListenerTest, should_keep_running_stats_right_after_a_correction)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
    RollAll(game, {3U, 4U, 5U, 2U}); /* frames of 7 and 7 */

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* frames of 9 and 7 */
    EXPECT_EQ(2, stats.Frames());
    EXPECT_DOUBLE_EQ(8.0, stats.Average());
}

TEST(CorrectionListenerTest, should_tell_the_listeners_when_a_correction_reopens_a_frame)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    KeyedScoreboard scoreboard;
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
    RollAll(game, {3U, 4U}); /* frame 1 complete: 7 */

    /* The first ball was really a strike: frame 1 now waits for a second bonus roll. */
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 10U));
    EXPECT_EQ(0U, Game_Score(game));
    EXPECT_TRUE(scoreboard.scores.empty());
    EXPECT_EQ(0, stats.Frames());

    RollAll(game, {2U}); /* its second bonus: frame 1 is 16, frame 2 is 6 */
    EXPECT_EQ((std::map<int, int>{{1, 16}, {2, 6}}), scoreboard.scores);
}

/* ---- A correction the game can't make -------------------------------------------------- */

TEST(CorrectionTest, should_reject_correcting_a_roll_that_has_not_been_made)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 0U, 5U)); /* rolls start at 1 */
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 3U, 5U)); /* only 2 so far */
    EXPECT_EQ(7U, Game_Score(game));
}

TEST(CorrectionTest, should_reject_correcting_a_null_game)
{
    EXPECT_EQ(GAME_ERR_NULL_GAME, Game_CorrectRoll(nullptr, 1U, 5U));
}

namespace {

/* A listener that tries to correct a roll from inside its own notification, once. */
struct CorrectsFromInside {
    Game *game = nullptr;
    bool tried = false;
    GameStatus status = GAME_OK;
};

void CorrectsFromInside_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                     bool frame_complete)
{
    (void)frame_number;
    (void)frame_score;
    (void)frame_complete;
    auto *listener = static_cast<CorrectsFromInside *>(context);
    if (!listener->tried) {
        listener->tried = true;
        listener->status = Game_CorrectRoll(listener->game, 1U, 5U);
    }
}

} // namespace

TEST(CorrectionTest, should_refuse_a_correction_made_from_inside_a_listener)
{
    /* The same hazard as a roll from inside a listener: a replay mid-notification would
     * tell listeners about frames out of order, or twice. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    CorrectsFromInside listener;
    listener.game = game;
    ASSERT_TRUE(Game_OnFrameChanged(game, &CorrectsFromInside_FrameChanged, &listener));

    RollAll(game, {3U, 4U});
    EXPECT_EQ(GAME_ERR_ROLL_DURING_NOTIFICATION, listener.status);
    EXPECT_EQ(7U, Game_Score(game)); /* the refused correction changed nothing */
}

/* ---- A property: a corrected game tells its listeners what a fresh game would ----------- */


TEST(CorrectionPropertyTest, should_leave_listeners_as_a_fresh_game_of_the_corrected_rolls_would)
{
    /* Random games (fixed seed, so it repeats), each corrected at a random roll. Whenever the
     * game accepts the correction, its listener must end up exactly where a listener on a
     * fresh game fed the corrected rolls from the start would be: the same frames, the same
     * scores, with any reopened frame dropped. Whenever it rejects one, nothing
     * may change. */
    std::mt19937 random(20260926U);
    int corrections_accepted = 0;
    int corrections_rejected = 0;
    for (int trial = 0; trial < 3000; trial++) {
        GameHandle owner = MakeGame();
        Game *game = owner.get();
        KeyedScoreboard scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));

        std::vector<Pins> rolls;
        const int length = 1 + static_cast<int>(random() % 21U);
        for (int tries = 0; (static_cast<int>(rolls.size()) < length) && (tries < 200); tries++) {
            const Pins pins = static_cast<Pins>(random() % 11U);
            if (Game_Roll(game, pins) == GAME_OK) {
                rolls.push_back(pins);
            }
        }

        const auto roll_number = static_cast<uint8_t>(1U + random() % rolls.size());
        const Pins corrected = static_cast<Pins>(random() % 11U);
        const std::map<int, int> scores_before = scoreboard.scores;
        const Score score_before = Game_Score(game);
        if (Game_CorrectRoll(game, roll_number, corrected) != GAME_OK) {
            /* A rejected correction changes nothing, and tells the listeners nothing new. */
            corrections_rejected++;
            ASSERT_EQ(scores_before, scoreboard.scores) << "trial " << trial;
            ASSERT_EQ(score_before, Game_Score(game)) << "trial " << trial;
            continue;
        }
        corrections_accepted++;
        rolls[roll_number - 1U] = corrected;

        GameHandle fresh_owner = MakeGame();
        Game *fresh = fresh_owner.get();
        KeyedScoreboard fresh_scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(fresh, &KeyedScoreboard_FrameChanged, &fresh_scoreboard));
        for (const Pins pins : rolls) {
            ASSERT_EQ(GAME_OK, Game_Roll(fresh, pins));
        }

        ASSERT_EQ(fresh_scoreboard.scores, scoreboard.scores) << "trial " << trial;
        ASSERT_EQ(Game_Score(fresh), Game_Score(game)) << "trial " << trial;
    }
    EXPECT_GT(corrections_accepted, 500); /* both halves of the property were really */
    EXPECT_GT(corrections_rejected, 500); /* exercised */
}

/* ---- After review ----------------------------------------------------------------------- */

TEST(CorrectionListenerTest, should_tell_the_listeners_nothing_when_a_correction_is_rejected)
{
    /* A rejected correction replays the corrected log, fails partway, then replays the
     * original. Its net effect is nothing, so the listeners must hear nothing at all. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {7U, 3U, 5U, 2U}); /* frames of 15 and 7 */

    std::vector<std::pair<int, int>> heard;
    auto record = [](void *context, uint8_t frame_number, Score frame_score, bool frame_complete) {
        static_cast<std::vector<std::pair<int, int>> *>(context)->emplace_back(
            frame_complete ? frame_number : -frame_number, frame_score);
    };
    ASSERT_TRUE(Game_OnFrameChanged(game, record, &heard));

    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_CorrectRoll(game, 1U, 8U)); /* 8 + 3 > 10 */
    EXPECT_TRUE(heard.empty());
    EXPECT_EQ(22U, Game_Score(game));
}

TEST(CorrectionTest, should_correct_the_first_and_last_rolls_of_the_longest_game)
{
    /* The longest game there is: nine open frames, then a spare and its fill ball in the
     * tenth, 21 rolls. Correcting roll 1 and roll 21 covers both ends of the log. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    for (int frame = 1; frame <= 9; frame++) {
        RollAll(game, {1U, 1U});
    }
    RollAll(game, {5U, 5U, 5U}); /* 18 + 15 = 33 */
    ASSERT_EQ(33U, Game_Score(game));

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 21U, 7U)); /* the fill ball was a 7 */
    EXPECT_EQ(35U, Game_Score(game));
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 3U)); /* the very first ball was a 3 */
    EXPECT_EQ(37U, Game_Score(game));
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 22U, 1U)); /* no 22nd roll */
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 1U)); /* still over after correcting */
}
