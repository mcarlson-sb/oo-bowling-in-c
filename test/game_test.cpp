/* Host tests for the Game API, one behavior per test. A frame scores 0 until it is
 * complete, so a strike or spare adds nothing until its bonus rolls are in. */
#include "test_support.h"

static void RollMany(Game *game, int count, Pins pins)
{
    for (int i = 0; i < count; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll " << i + 1 << " was rejected";
    }
}

/* Nine frames of gutter balls: the next roll starts the tenth frame. */
static void RollToTenthFrame(Game *game)
{
    RollMany(game, 18, 0U);
}

class GameTest : public ::testing::Test {
protected:
    GameHandle owner = MakeGame();
    Game *game = owner.get();
};

/* ---- Scoring ---------------------------------------------------------------------------- */

TEST_F(GameTest, should_be_created)
{
    EXPECT_NE(nullptr, game);
}

TEST_F(GameTest, should_get_a_score_of_0_from_a_new_game)
{
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_get_a_score_of_0_from_an_incomplete_frame)
{
    RollAll(game, {2U});
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_get_a_score_of_8_from_a_regular_frame_rolls_2_6)
{
    RollAll(game, {2U, 6U});
    EXPECT_EQ(8U, Game_Score(game));
}

TEST_F(GameTest, should_score_5_from_rolls_2_3_4)
{
    RollAll(game, {2U, 3U, 4U});
    EXPECT_EQ(5U, Game_Score(game));
}

TEST_F(GameTest, should_score_14_from_two_complete_frames_rolls_2_3_4_5)
{
    RollAll(game, {2U, 3U, 4U, 5U});
    EXPECT_EQ(14U, Game_Score(game));
}

TEST_F(GameTest, should_score_an_unfinished_spare_as_0)
{
    RollAll(game, {8U, 2U});
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_complete_spare_roll_8_2_1)
{
    RollAll(game, {8U, 2U, 1U});
    EXPECT_EQ(11U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_complete_spare_and_complete_regular_roll_8_2_1_4)
{
    RollAll(game, {8U, 2U, 1U, 4U});
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_score_an_unfinished_strike_as_0)
{
    RollAll(game, {10U, 1U});
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_complete_strike)
{
    RollAll(game, {10U, 1U, 2U});
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_score_correctly_with_a_gutter_ball)
{
    RollAll(game, {9U, 0U, 1U, 2U});
    EXPECT_EQ(12U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_full_game_correctly)
{
    /* The running total after every roll, one row per roll. */
    const struct {
        Pins pins;
        Score expected_score;
    } rolls[] = {
        {10U, 0U},   {9U, 0U},    {1U, 20U},   {5U, 35U},   {5U, 35U},   {7U, 52U},
        {2U, 61U},   {10U, 61U},  {10U, 61U},  {10U, 91U},  {9U, 120U},  {0U, 148U},
        {8U, 148U},  {2U, 148U},  {9U, 167U},  {1U, 167U},  {10U, 187U},
    };

    for (const auto &roll : rolls) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, roll.pins)) << "rolling " << +roll.pins;
        EXPECT_EQ(roll.expected_score, Game_Score(game)) << "after rolling " << +roll.pins;
    }
}

/* ---- End of game ------------------------------------------------------------------------
 * A game holds exactly ten frames, with no heap. A roll after the game is over is
 * reported, not written out of bounds, and leaves the game unchanged. */

TEST_F(GameTest, should_score_a_perfect_game_and_reject_a_13th_roll)
{
    RollMany(game, 12, 10U);
    EXPECT_EQ(300U, Game_Score(game));

    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 10U));
    EXPECT_EQ(300U, Game_Score(game));
}

/* ---- Game storage -----------------------------------------------------------------------
 * Games come from a fixed pool, so Game_Create can run out. */

TEST_F(GameTest, should_keep_two_games_independent)
{
    GameHandle other = MakeGame();
    ASSERT_NE(nullptr, other);

    RollAll(game, {2U, 6U});
    RollAll(other.get(), {1U, 1U});

    EXPECT_EQ(8U, Game_Score(game));
    EXPECT_EQ(2U, Game_Score(other.get()));
}

TEST_F(GameTest, should_return_null_when_no_game_is_free_and_reuse_a_destroyed_one)
{
    GameHandle second = MakeGame();
    ASSERT_NE(nullptr, second);
    GameHandle third = MakeGame();
    EXPECT_EQ(nullptr, third);

    second.reset();
    GameHandle reused = MakeGame();
    ASSERT_NE(nullptr, reused);
    EXPECT_EQ(0U, Game_Score(reused.get()));
}

/* ---- Tenth frame ------------------------------------------------------------------------
 * Fill balls belong to the tenth frame and are scored once. They must not spill into a
 * frame after it. */

TEST_F(GameTest, should_score_a_tenth_frame_strike_whose_fill_balls_leave_pins_standing)
{
    RollToTenthFrame(game);
    RollAll(game, {10U, 3U, 3U});
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_end_the_game_after_an_open_tenth_frame)
{
    RollMany(game, 20, 1U);
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 1U));
    EXPECT_EQ(20U, Game_Score(game));
}

TEST_F(GameTest, should_give_a_tenth_frame_spare_exactly_one_fill_ball)
{
    RollToTenthFrame(game);
    RollAll(game, {5U, 5U, 5U});
    EXPECT_EQ(15U, Game_Score(game));
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 5U));
}

/* ---- Input validation -------------------------------------------------------------------
 * A roll of more pins than are standing is rejected and leaves the game unchanged. */

TEST_F(GameTest, should_reject_a_roll_of_more_than_ten_pins)
{
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 11U));
    RollAll(game, {3U, 4U});
    EXPECT_EQ(7U, Game_Score(game));
}

TEST_F(GameTest, should_reject_a_second_roll_that_knocks_down_more_pins_than_are_standing)
{
    RollAll(game, {7U});
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 4U));
    RollAll(game, {2U});
    EXPECT_EQ(9U, Game_Score(game));
}

TEST_F(GameTest, should_reject_a_tenth_frame_second_roll_larger_than_the_pins_standing)
{
    RollToTenthFrame(game);
    RollAll(game, {7U});
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 4U));
    RollAll(game, {3U, 10U});
    EXPECT_EQ(20U, Game_Score(game));
}

TEST_F(GameTest, should_reject_tenth_frame_strike_fill_balls_totalling_more_than_ten)
{
    RollToTenthFrame(game);
    RollAll(game, {10U, 5U});
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 6U));
    RollAll(game, {5U});
    EXPECT_EQ(20U, Game_Score(game));
}

/* ---- NULL handles -----------------------------------------------------------------------
 * Game_Create returns NULL when the pool is empty, so every function accepts NULL. */

TEST_F(GameTest, should_reject_a_roll_on_a_null_game)
{
    EXPECT_EQ(GAME_ERR_NULL_GAME, Game_Roll(nullptr, 3U));
}

TEST_F(GameTest, should_score_a_null_game_as_0)
{
    EXPECT_EQ(0U, Game_Score(nullptr));
}

TEST_F(GameTest, should_ignore_destroying_a_null_game)
{
    Game_Destroy(nullptr);

    /* Nothing was freed: the one free slot is still free, and this game still works. */
    GameHandle other = MakeGame();
    EXPECT_NE(nullptr, other);
    RollAll(game, {3U, 4U});
    EXPECT_EQ(7U, Game_Score(game));
}
