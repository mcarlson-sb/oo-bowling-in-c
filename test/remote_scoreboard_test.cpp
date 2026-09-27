/* A remote scoreboard: the far side of a wire. On the near side, a listener writes each
 * frame-changed message into a byte buffer, as a serial link or a socket would carry it. On
 * the far side, a decoder that has only those bytes rebuilds the scoreboard.
 *
 * With the pinsetter, this is one boundary crossed in both directions: rolls come in from a
 * machine the game doesn't own, and scores go out to a display it doesn't own. Neither side
 * holds a pointer into the game. */

#include "pinsetter.h"

#include <cstdint>
#include <map>
#include <vector>

#include "test_support.h"

namespace {

using Bytes = std::vector<uint8_t>;

/* Near side. One message is 4 bytes: the frame number, the score (low byte, then high), and
 * whether the frame is complete. */
void WireWriter_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                             bool frame_complete)
{
    auto *wire = static_cast<Bytes *>(context);
    wire->push_back(frame_number);
    wire->push_back(static_cast<uint8_t>(frame_score & 0xFFU));
    wire->push_back(static_cast<uint8_t>(frame_score >> 8U));
    wire->push_back(frame_complete ? 1U : 0U);
}

/* Far side. Knows the wire format and nothing else: not the game, not its headers' types. */
struct RemoteScoreboard {
    std::map<int, int> frames; /* complete frames: frame number -> score */
    size_t read = 0U;          /* bytes of the wire already decoded */

    void Decode(const Bytes &wire)
    {
        for (; (read + 4U) <= wire.size(); read += 4U) {
            const int frame_number = wire[read];
            const int score = wire[read + 1U] | (wire[read + 2U] << 8U);
            if (wire[read + 3U] != 0U) {
                frames[frame_number] = score; /* new, or an update after a correction */
            } else {
                frames.erase(frame_number); /* reopened by a correction */
            }
        }
    }
};

using PinsetterHandle = std::unique_ptr<Pinsetter, decltype(&Pinsetter_Destroy)>;

} // namespace

TEST(RemoteScoreboardTest, should_rebuild_the_scoreboard_from_the_bytes_alone)
{
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle pinsetter_owner(Pinsetter_Create(), &Pinsetter_Destroy);
    Pinsetter *pinsetter = pinsetter_owner.get();
    Bytes wire;
    RunningStats local; /* the same news, heard directly, for comparison */
    ASSERT_TRUE(Game_OnFrameChanged(game, &WireWriter_FrameChanged, &wire));
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &local));
    RemoteScoreboard remote;

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 4U));
    Pinsetter_Drain(pinsetter, game);
    remote.Decode(wire);
    EXPECT_EQ((std::map<int, int>{{1, 7}}), remote.frames);

    /* The first ball was really a strike: frame 1 reopens, waiting for its second bonus. */
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 10U));
    remote.Decode(wire);
    EXPECT_EQ((std::map<int, int>{}), remote.frames);

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 5U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 2U));
    Pinsetter_Drain(pinsetter, game);
    remote.Decode(wire);
    EXPECT_EQ((std::map<int, int>{{1, 19}, {2, 9}}), remote.frames); /* 10+4+5, then 4+5 */
    EXPECT_EQ(local.frame_scores, remote.frames);
}
