/* Host tests for the Game API, one behavior per test. A frame scores 0 until it is
 * complete, so a strike or spare adds nothing until its bonus rolls are in. */
#include <gtest/gtest.h>

#include <memory>

#include "game.h"

/* Games come from a fixed pool that outlives each test. Holding every game in a handle that
 * destroys it means a test can't leak a pool slot into later tests, however it ends. */
using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

static GameHandle MakeGame()
{
    return GameHandle(Game_Create(), &Game_Destroy);
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

TEST_F(GameTest, should_get_a_score_of_0_from_an_open_frame)
{
    Game_Roll(game, 2U);
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_get_a_score_of_8_from_a_regular_frame_rolls_2_6)
{
    Game_Roll(game, 2U);
    Game_Roll(game, 6U);
    EXPECT_EQ(8U, Game_Score(game));
}

TEST_F(GameTest, should_score_5_from_rolls_2_3_4)
{
    Game_Roll(game, 2U);
    Game_Roll(game, 3U);
    Game_Roll(game, 4U);
    EXPECT_EQ(5U, Game_Score(game));
}

TEST_F(GameTest, should_score_14_from_two_closed_frames_rolls_2_3_4_5)
{
    Game_Roll(game, 2U);
    Game_Roll(game, 3U);
    Game_Roll(game, 4U);
    Game_Roll(game, 5U);
    EXPECT_EQ(14U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_spare_as_open)
{
    Game_Roll(game, 8U);
    Game_Roll(game, 2U);
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_closed_spare_roll_8_2_1)
{
    Game_Roll(game, 8U);
    Game_Roll(game, 2U);
    Game_Roll(game, 1U);
    EXPECT_EQ(11U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_closed_spare_and_closed_regular_roll_8_2_1_4)
{
    Game_Roll(game, 8U);
    Game_Roll(game, 2U);
    Game_Roll(game, 1U);
    Game_Roll(game, 4U);
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_score_an_open_strike)
{
    Game_Roll(game, 10U);
    Game_Roll(game, 1U);
    EXPECT_EQ(0U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_closed_strike)
{
    Game_Roll(game, 10U);
    Game_Roll(game, 1U);
    Game_Roll(game, 2U);
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_score_correctly_with_a_gutter_ball)
{
    Game_Roll(game, 9U);
    Game_Roll(game, 0U);
    Game_Roll(game, 1U);
    Game_Roll(game, 2U);
    EXPECT_EQ(12U, Game_Score(game));
}

TEST_F(GameTest, should_score_a_full_game_correctly)
{
    /* The running total after every roll, one row per roll. */
    const struct {
        uint8_t pins;
        uint16_t expected_score;
    } rolls[] = {
        {10U, 0U},   {9U, 0U},    {1U, 20U},   {5U, 35U},   {5U, 35U},   {7U, 52U},
        {2U, 61U},   {10U, 61U},  {10U, 61U},  {10U, 91U},  {9U, 120U},  {0U, 148U},
        {8U, 148U},  {2U, 148U},  {9U, 167U},  {1U, 167U},  {10U, 187U},
    };

    for (const auto &roll : rolls) {
        Game_Roll(game, roll.pins);
        EXPECT_EQ(roll.expected_score, Game_Score(game)) << "after rolling " << +roll.pins;
    }
}

/* ---- End of game ------------------------------------------------------------------------
 * A game holds exactly ten frames, with no heap. A roll after the game is over is
 * reported, not written out of bounds, and leaves the game unchanged. */

TEST_F(GameTest, should_score_a_perfect_game_and_reject_a_13th_roll)
{
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 10U));
    }
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

    Game_Roll(game, 2U);
    Game_Roll(game, 6U);
    Game_Roll(other.get(), 1U);
    Game_Roll(other.get(), 1U);

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

TEST_F(GameTest, should_ignore_destroying_a_null_game)
{
    Game_Destroy(nullptr);
    EXPECT_NE(nullptr, game);
}

/* ---- Tenth frame ------------------------------------------------------------------------
 * Fill balls belong to the tenth frame and are scored once. They must not spill into a
 * frame after it. */

TEST_F(GameTest, should_score_a_tenth_frame_strike_with_open_fill_balls)
{
    for (int i = 0; i < 18; i++) {
        Game_Roll(game, 0U);
    }
    Game_Roll(game, 10U);
    Game_Roll(game, 3U);
    Game_Roll(game, 3U);
    EXPECT_EQ(16U, Game_Score(game));
}

TEST_F(GameTest, should_end_the_game_after_an_open_tenth_frame)
{
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 1U));
    }
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 1U));
    EXPECT_EQ(20U, Game_Score(game));
}

TEST_F(GameTest, should_give_a_tenth_frame_spare_exactly_one_fill_ball)
{
    for (int i = 0; i < 18; i++) {
        Game_Roll(game, 0U);
    }
    Game_Roll(game, 5U);
    Game_Roll(game, 5U);
    EXPECT_EQ(GAME_OK, Game_Roll(game, 5U));
    EXPECT_EQ(15U, Game_Score(game));
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 5U));
}

/* ---- Input validation -------------------------------------------------------------------
 * A roll of more pins than are standing is rejected and leaves the game unchanged. */

TEST_F(GameTest, should_reject_a_roll_of_more_than_ten_pins)
{
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 11U));
    Game_Roll(game, 3U);
    Game_Roll(game, 4U);
    EXPECT_EQ(7U, Game_Score(game));
}

TEST_F(GameTest, should_reject_a_second_roll_that_knocks_down_more_pins_than_are_standing)
{
    Game_Roll(game, 7U);
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 4U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 2U));
    EXPECT_EQ(9U, Game_Score(game));
}

TEST_F(GameTest, should_reject_a_tenth_frame_second_roll_larger_than_the_pins_standing)
{
    for (int i = 0; i < 18; i++) {
        Game_Roll(game, 0U);
    }
    Game_Roll(game, 7U);
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 4U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 3U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 10U));
    EXPECT_EQ(20U, Game_Score(game));
}

TEST_F(GameTest, should_reject_tenth_frame_strike_fill_balls_totalling_more_than_ten)
{
    for (int i = 0; i < 18; i++) {
        Game_Roll(game, 0U);
    }
    Game_Roll(game, 10U);
    Game_Roll(game, 5U);
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_Roll(game, 6U));
    EXPECT_EQ(GAME_OK, Game_Roll(game, 5U));
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
