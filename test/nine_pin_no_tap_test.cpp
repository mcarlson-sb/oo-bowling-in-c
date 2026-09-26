/* A client of the library that plays nine-pin no-tap: knocking down 9 on a first ball
 * counts as a strike. The rule is written here, by the client. The library contains no
 * trace of it; it only lets a caller say how a roll is counted. */
#include <gtest/gtest.h>

#include <memory>

#include "game.h"

namespace {

/* Nine-pin no-tap: a first ball, on a full rack, that leaves one pin standing counts as a
 * strike. Every other roll counts as the pins it knocked down. */
Pins NinePinNoTap(Pins pins_standing, Pins pins_down)
{
    const bool nine_on_a_full_rack = (pins_standing == 10U) && (pins_down == 9U);
    return nine_on_a_full_rack ? static_cast<Pins>(10U) : pins_down;
}

using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

class NinePinNoTapTest : public ::testing::Test {
protected:
    GameHandle owner{Game_CreateWithRule(&NinePinNoTap), &Game_Destroy};
    Game *game = owner.get();

    void RollAll(std::initializer_list<Pins> rolls)
    {
        for (const Pins pins : rolls) {
            EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
        }
    }
};

} // namespace

TEST_F(NinePinNoTapTest, should_score_a_first_ball_nine_as_a_strike)
{
    RollAll({9U, 3U, 4U});
    EXPECT_EQ(24U, Game_Score(game)); /* (10 + 3 + 4) + (3 + 4) */
}
