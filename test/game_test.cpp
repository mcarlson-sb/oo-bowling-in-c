/* Port of Bowling-OO game.service.spec.ts. Test names and order follow the spec. */
#include <gtest/gtest.h>

#include "game.h"

class GameTest : public ::testing::Test {
protected:
    Game *game = nullptr;

    void SetUp() override { game = Game_Create(); }
    void TearDown() override { Game_Destroy(game); }
};

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
    /* Same rolls and running totals as the spec, one row per roll. */
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

/* ---- C-specific: fixed capacity ---------------------------------------------------------
 * TypeScript grows its frames array without limit. With no heap, the C version has a fixed
 * number of frames, so rolling past it must be reported, not written out of bounds. */

TEST_F(GameTest, should_score_a_perfect_game_and_reject_a_roll_past_capacity)
{
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, 10U));
    }
    EXPECT_EQ(300U, Game_Score(game));

    EXPECT_EQ(GAME_ERR_FULL, Game_Roll(game, 10U));
    EXPECT_EQ(300U, Game_Score(game));
}

/* ---- C-specific: game storage -----------------------------------------------------------
 * `new GameService()` becomes a fixed pool of games. */

TEST_F(GameTest, should_keep_two_games_independent)
{
    Game *other = Game_Create();
    ASSERT_NE(nullptr, other);

    Game_Roll(game, 2U);
    Game_Roll(game, 6U);
    Game_Roll(other, 1U);
    Game_Roll(other, 1U);

    EXPECT_EQ(8U, Game_Score(game));
    EXPECT_EQ(2U, Game_Score(other));
    Game_Destroy(other);
}

TEST_F(GameTest, should_return_null_when_no_game_is_free_and_reuse_a_destroyed_one)
{
    Game *second = Game_Create();
    ASSERT_NE(nullptr, second);
    EXPECT_EQ(nullptr, Game_Create());

    Game_Destroy(second);
    Game *reused = Game_Create();
    ASSERT_NE(nullptr, reused);
    EXPECT_EQ(0U, Game_Score(reused));
    Game_Destroy(reused);
}
