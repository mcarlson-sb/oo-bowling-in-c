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

TEST_F(NinePinNoTapTest, should_count_a_no_tap_strike_as_ten_in_an_earlier_strikes_bonus)
{
    RollAll({10U, 9U, 3U, 4U});
    EXPECT_EQ(47U, Game_Score(game)); /* (10 + 10 + 3) + (10 + 3 + 4) + (3 + 4) */
}

TEST_F(NinePinNoTapTest, should_count_no_tap_strikes_in_the_tenth_frames_fill_balls)
{
    for (int i = 0; i < 18; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U));
    }
    RollAll({10U, 9U, 9U}); /* in standard bowling the second 9 would be too many pins */
    EXPECT_EQ(30U, Game_Score(game));
}

/* ---- A rule is caller code, so the library can't take it on trust ----------------------- */

namespace {

/* A buggy rule: counts a strike as more pins than there are. */
Pins CountsTooMany(Pins pins_standing, Pins pins_down)
{
    return (pins_down == pins_standing) ? static_cast<Pins>(pins_standing + 1U) : pins_down;
}

} // namespace

TEST(BrokenRuleTest, should_reject_a_roll_its_rule_counts_as_more_pins_than_were_standing)
{
    GameHandle owner{Game_CreateWithRule(&CountsTooMany), &Game_Destroy};
    Game *game = owner.get();

    EXPECT_EQ(GAME_ERR_RULE_OUT_OF_RANGE, Game_Roll(game, 10U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 3U)); /* the game is unchanged, and still playable */
    EXPECT_EQ(GAME_OK, Game_Roll(game, 4U));
    EXPECT_EQ(7U, Game_Score(game));
}

TEST(BrokenRuleTest, should_refuse_to_create_a_game_without_a_rule)
{
    GameHandle owner{Game_CreateWithRule(nullptr), &Game_Destroy};
    EXPECT_EQ(nullptr, owner.get());
}
