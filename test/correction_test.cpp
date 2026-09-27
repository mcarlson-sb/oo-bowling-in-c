/* A scorer fixing a roll entered wrongly: the game rescores everything after it. */
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
