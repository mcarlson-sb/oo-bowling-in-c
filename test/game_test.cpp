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
