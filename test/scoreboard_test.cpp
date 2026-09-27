/* A client that keeps a live scoreboard: it is told each time a frame completes, with the
 * frame's number and score. */
#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "game.h"

namespace {

using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

/* The scoreboard: every completed frame it has been told about, in the order it heard. */
struct Scoreboard {
    std::vector<std::pair<int, int>> frames; /* (frame number, frame score) */
};

void Scoreboard_FrameCompleted(void *context, uint8_t frame_number, Score frame_score)
{
    auto *scoreboard = static_cast<Scoreboard *>(context);
    scoreboard->frames.emplace_back(frame_number, frame_score);
}

class ScoreboardTest : public ::testing::Test {
protected:
    GameHandle owner{Game_Create(), &Game_Destroy};
    Game *game = owner.get();
    Scoreboard scoreboard;

    void SetUp() override { Game_OnFrameCompleted(game, &Scoreboard_FrameCompleted, &scoreboard); }

    void RollAll(std::initializer_list<Pins> rolls)
    {
        for (const Pins pins : rolls) {
            EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins;
        }
    }
};

using Frames = std::vector<std::pair<int, int>>;

} // namespace

TEST_F(ScoreboardTest, should_tell_the_scoreboard_when_a_frame_completes)
{
    RollAll({3U, 4U});
    EXPECT_EQ((Frames{{1, 7}}), scoreboard.frames);
}
