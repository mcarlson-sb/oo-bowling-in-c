/* A client of the library that plays nine-pin no-tap: knocking down 9 on a first ball
 * counts as a strike. The rule is client code, in test_support.h. The library contains no
 * trace of it; it only lets a caller say how a roll is counted. */
#include "test_support.h"

namespace {

/* NinePinNoTap, the client's rule, is in test_support.h, shared with the scoreboard
 * tests. */
class NinePinNoTapTest : public ::testing::Test {
protected:
    GameHandle owner = MakeGameWithRule(&NinePinNoTap);
    Game *game = owner.get();
};

} // namespace

TEST_F(NinePinNoTapTest, should_score_a_first_ball_nine_as_a_strike)
{
    RollAll(game, {9U, 3U, 4U});
    EXPECT_EQ(24U, Game_Score(game)); /* (10 + 3 + 4) + (3 + 4) */
}

TEST_F(NinePinNoTapTest, should_count_a_no_tap_strike_as_ten_in_an_earlier_strikes_bonus)
{
    RollAll(game, {10U, 9U, 3U, 4U});
    EXPECT_EQ(47U, Game_Score(game)); /* (10 + 10 + 3) + (10 + 3 + 4) + (3 + 4) */
}

TEST_F(NinePinNoTapTest, should_count_no_tap_strikes_in_the_tenth_frames_fill_balls)
{
    for (int i = 0; i < 18; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 0U));
    }
    RollAll(game, {10U, 9U, 9U}); /* in standard bowling the second 9 would be too many pins */
    EXPECT_EQ(30U, Game_Score(game));
}

TEST_F(NinePinNoTapTest, should_not_count_5_then_4_as_a_spare_under_a_first_ball_rule)
{
    RollAll(game, {5U, 4U, 3U});
    EXPECT_EQ(9U, Game_Score(game)); /* an open 5 + 4; the 3 starts frame 2 */
}

namespace {

/* The stricter form many no-tap leagues use: any ball that leaves exactly one pin standing
 * clears the rack, on a first ball (a strike) or a second (a spare). */
Pins OnePinLeftClearsTheRack(Pins pins_standing, Pins pins_down)
{
    const bool one_pin_left = (pins_standing >= 1U) && ((pins_down + 1U) == pins_standing);
    return one_pin_left ? pins_standing : pins_down;
}

} // namespace

TEST(OnePinLeftRuleTest, should_count_5_then_4_as_a_spare_under_a_one_pin_left_rule)
{
    GameHandle owner = MakeGameWithRule(&OnePinLeftClearsTheRack);
    Game *game = owner.get();
    for (const Pins pins : {Pins{5U}, Pins{4U}, Pins{3U}}) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
    }
    EXPECT_EQ(13U, Game_Score(game)); /* a spare, 5 + 5, plus its bonus 3 */
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
    GameHandle owner = MakeGameWithRule(&CountsTooMany);
    Game *game = owner.get();

    EXPECT_EQ(GAME_ERR_RULE_OUT_OF_RANGE, Game_Roll(game, 10U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 3U)); /* the game is unchanged, and still playable */
    EXPECT_EQ(GAME_OK, Game_Roll(game, 4U));
    EXPECT_EQ(7U, Game_Score(game));
}

TEST(BrokenRuleTest, should_refuse_to_create_a_game_without_a_rule)
{
    GameHandle owner = MakeGameWithRule(nullptr);
    EXPECT_EQ(nullptr, owner.get());
}

namespace {

/* A rule that breaks the contract a different way: it reaches back into its own game, through
 * a global, the first time it is asked, and tries to roll and to correct a roll. */
Game *s_rule_game = nullptr;
bool s_rule_reached_back = false;
GameStatus s_roll_from_rule = GAME_OK;
GameStatus s_correction_from_rule = GAME_OK;

Pins ReachesBackIntoTheGame(Pins pins_standing, Pins pins_down)
{
    (void)pins_standing;
    if (!s_rule_reached_back) {
        s_rule_reached_back = true;
        s_roll_from_rule = Game_Roll(s_rule_game, 1U);
        s_correction_from_rule = Game_CorrectRoll(s_rule_game, 1U, 1U);
    }
    return pins_down;
}

} // namespace

TEST(BrokenRuleTest, should_refuse_a_roll_or_an_edit_made_from_inside_the_rule)
{
    /* The rule runs in the middle of a roll, with the frames half updated. A roll or an edit
     * from there would change the game under the roll being counted, so both are refused,
     * like a change from inside a listener, and the game ends with only the real rolls. */
    GameHandle owner = MakeGameWithRule(&ReachesBackIntoTheGame);
    s_rule_game = owner.get();
    s_rule_reached_back = false;

    RollAll(s_rule_game, {3U, 4U});

    EXPECT_EQ(GAME_ERR_BUSY, s_roll_from_rule);
    EXPECT_EQ(GAME_ERR_BUSY, s_correction_from_rule);
    EXPECT_EQ(7U, Game_Score(s_rule_game));
}
