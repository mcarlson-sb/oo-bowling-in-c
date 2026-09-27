/* A scorer fixing a roll entered wrongly: the game rescores everything after it. */
#include <map>
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

void KeyedScoreboard_FrameCompleted(void *context, uint8_t frame_number, Score frame_score)
{
    static_cast<KeyedScoreboard *>(context)->scores[frame_number] = frame_score;
}

} // namespace

TEST(CorrectionListenerTest, should_tell_the_scoreboard_the_rescored_frames)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    KeyedScoreboard scoreboard;
    ASSERT_TRUE(Game_OnFrameCompleted(game, &KeyedScoreboard_FrameCompleted, &scoreboard));
    RollAll(game, {3U, 4U, 5U, 2U});
    ASSERT_EQ((std::map<int, int>{{1, 7}, {2, 7}}), scoreboard.scores);

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* frame 1 was 5, 4 */
    EXPECT_EQ((std::map<int, int>{{1, 9}, {2, 7}}), scoreboard.scores);
}
