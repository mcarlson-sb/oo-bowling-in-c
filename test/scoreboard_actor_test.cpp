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

TEST(ScoreboardTest, should_not_understand_a_roll_and_count_it)
{
    Scoreboard board = MakeBoard();
    Message roll = {};
    roll.envelope.selector = MSG_ROLL;
    roll.envelope.from = kAsker;
    roll.envelope.to = kBoard;
    roll.envelope.seq = 6U;
    const Outbox outbox = Send(&board, {roll});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, outbox.items[0].envelope.selector);
    EXPECT_EQ(kAsker, outbox.items[0].envelope.to);
    EXPECT_EQ(MSG_ROLL, outbox.items[0].payload.not_understood.selector);
    EXPECT_EQ(1U, board.not_understood);
}

TEST(ScoreboardTest, should_take_the_reply_to_its_subscription_in_silence)
{
    /* Subscribed on its behalf, it hears the game's reply first: not a request to answer. */
    Scoreboard board = MakeBoard();
    Message reply = {};
    reply.envelope.selector = MSG_REPLY;
    reply.envelope.from = kGame;
    reply.envelope.to = kBoard;
    const Outbox outbox = Send(&board, {reply});
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(0U, board.not_understood);
}

TEST(ScoreboardTest, should_ignore_a_frame_number_outside_the_frames_it_keeps)
{
    /* The frame number is a byte any sender fills in. */
    Scoreboard board = MakeBoard();
    const Outbox outbox =
        Send(&board, {FrameChanged(0, 5, true), FrameChanged(11, 5, true), Query(1U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}

TEST(ScoreboardTest, should_take_a_frames_new_score_and_its_reopening_as_the_game_tells_them)
{
    /* An edit's events: every frame again, with complete = false for one it reopened. */
    Scoreboard board = MakeBoard();
    Outbox outbox = Send(&board, {FrameChanged(1, 7, true), FrameChanged(1, 9, true), Query(1U)});
    EXPECT_EQ(9U, outbox.items[0].payload.reply.score);
    outbox = Send(&board, {FrameChanged(1, 0, false), Query(2U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}
