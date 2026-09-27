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
