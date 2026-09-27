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
