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
