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
