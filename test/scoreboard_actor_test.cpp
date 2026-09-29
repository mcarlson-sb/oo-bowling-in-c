/* The scoreboard kind: a subscriber that rebuilds the frames from the events it hears, and answers
 * QUERY_SCORE with their total. Driven by messages alone, as any actor is. */

#include <gtest/gtest.h>

extern "C" {
#include "scoreboard.h"
}

namespace {

constexpr ActorId kGame = 1U;
constexpr ActorId kBoard = 3U;
constexpr ActorId kAsker = 4U;

Scoreboard MakeBoard()
{
    Scoreboard board;
    Scoreboard_Init(&board, kBoard);
    return board;
}

Message FrameChanged(int frame, int score, bool complete)
{
    Message message = {};
    message.envelope.selector = MSG_FRAME_CHANGED;
    message.envelope.from = kGame;
    message.envelope.to = kBoard;
    message.payload.frame = {static_cast<FrameNumber>(frame), static_cast<Score>(score), complete};
    return message;
}

Message Query(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_SCORE;
    message.envelope.from = kAsker;
    message.envelope.to = kBoard;
    message.envelope.seq = seq;
    return message;
}

/* Sends each message in turn, with the outbox emptied first, and returns what the last one sent. */
Outbox Send(Scoreboard *board, std::initializer_list<Message> messages)
{
    Outbox outbox;
    for (const Message &message : messages) {
        outbox.count = 0U;
        Scoreboard_Handle(board, &message, &outbox);
    }
    return outbox;
}

} // namespace

TEST(ScoreboardTest, should_answer_the_total_of_the_frames_it_has_heard_complete)
{
    Scoreboard board = MakeBoard();
    const Outbox outbox = Send(&board, {FrameChanged(1, 7, true), FrameChanged(2, 12, true),
                                        FrameChanged(3, 0, false), Query(5U)});
    ASSERT_EQ(1U, outbox.count);
    const Message &reply = outbox.items[0];
    EXPECT_EQ(MSG_REPLY, reply.envelope.selector);
    EXPECT_EQ(kAsker, reply.envelope.to);
    EXPECT_EQ(kBoard, reply.envelope.from);
    EXPECT_EQ(5U, reply.envelope.seq);
    EXPECT_EQ(GAME_OK, reply.payload.reply.status);
    EXPECT_EQ(19U, reply.payload.reply.score);
}
