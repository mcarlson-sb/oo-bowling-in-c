/* The scoreboard kind: a subscriber that rebuilds the frames from the events it hears, and answers
 * QUERY_FIGURE with their total. Driven by messages alone, as any actor is. */

#include <gtest/gtest.h>

#include "test_outbox.h"

#include <cstring>

extern "C" {
#include "scoreboard.h"
#include "scoreboard_state.h"
}

namespace {

constexpr ActorId kGame = 1U;
constexpr ActorId kBoard = 3U;
constexpr ActorId kAsker = 4U;

Scoreboard MakeBoard()
{
    Scoreboard board;
    Scoreboard_Init(&board);
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
    message.envelope.selector = MSG_QUERY_FIGURE;
    message.envelope.from = kAsker;
    message.envelope.to = kBoard;
    message.envelope.seq = seq;
    return message;
}

/* Sends each message in turn, with the outbox emptied first, and returns what the last one sent. */
TestOutbox Send(Scoreboard *board, std::initializer_list<Message> messages)
{
    TestOutbox outbox(SCOREBOARD_MOST_SENT);
    for (const Message &message : messages) {
        outbox.count = 0U;
        Scoreboard_Handle(board, &message, &outbox);
    }
    return outbox;
}


/* Its not-understood count, asked for rather than read from its state. */
uint16_t NotUnderstoodCount(Scoreboard *board)
{
    Message ask = Query(99U);
    ask.envelope.selector = MSG_QUERY_STATS;
    const TestOutbox outbox = Send(board, {ask});
    EXPECT_EQ(MSG_STATS, outbox.items[0].envelope.selector);
    return outbox.items[0].payload.stats.not_understood;
}

} // namespace

TEST(ScoreboardTest, should_answer_the_total_of_the_frames_it_has_heard_complete)
{
    Scoreboard board = MakeBoard();
    const TestOutbox outbox = Send(&board, {FrameChanged(1, 7, true), FrameChanged(2, 12, true),
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
    const TestOutbox outbox = Send(&board, {roll});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, outbox.items[0].envelope.selector);
    EXPECT_EQ(1U, NotUnderstoodCount(&board));
}

TEST(ScoreboardTest, should_take_the_reply_to_its_subscription_in_silence)
{
    /* Subscribed on its behalf, it hears the game's reply first: not a request to answer. */
    Scoreboard board = MakeBoard();
    Message reply = {};
    reply.envelope.selector = MSG_REPLY;
    reply.envelope.from = kGame;
    reply.envelope.to = kBoard;
    const TestOutbox outbox = Send(&board, {reply});
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(0U, NotUnderstoodCount(&board));
}

TEST(ScoreboardTest, should_ignore_a_frame_number_outside_the_frames_it_keeps)
{
    /* The frame number is a byte any sender fills in. */
    Scoreboard board = MakeBoard();
    const TestOutbox outbox =
        Send(&board, {FrameChanged(0, 5, true), FrameChanged(11, 5, true), Query(1U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}

TEST(ScoreboardTest, should_take_a_frames_new_score_and_its_reopening_as_the_game_tells_them)
{
    /* An edit's events: every frame again, with complete = false for one it reopened. */
    Scoreboard board = MakeBoard();
    TestOutbox outbox = Send(&board, {FrameChanged(1, 7, true), FrameChanged(1, 9, true), Query(1U)});
    EXPECT_EQ(9U, outbox.items[0].payload.reply.score);
    outbox = Send(&board, {FrameChanged(1, 0, false), Query(2U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}

/* ---- Pinned at the phase 3 stop, from mutation testing -------------------------------------- */

TEST(ScoreboardTest, should_start_empty_whatever_memory_it_is_given)
{
    /* A hosted instance is reused; a stack's is whatever was there. */
    Scoreboard board;
    std::memset(&board, 0xFF, sizeof(board));
    Scoreboard_Init(&board);
    const TestOutbox outbox = Send(&board, {Query(1U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
    EXPECT_EQ(0U, NotUnderstoodCount(&board));
}

TEST(ScoreboardTest, should_keep_the_last_frame_the_game_has)
{
    Scoreboard board = MakeBoard();
    const TestOutbox outbox = Send(&board, {FrameChanged(10, 30, true), Query(1U)});
    EXPECT_EQ(30U, outbox.items[0].payload.reply.score);
}

TEST(ScoreboardTest, should_take_the_rest_of_what_a_game_tells_subscribers_in_silence)
{
    /* A game tells every subscriber of rolls held and lost too. Answered NOT_UNDERSTOOD, a burst
     * of held rolls became a burst of NOT_UNDERSTOODs into the game's own mailbox. */
    Scoreboard board = MakeBoard();
    Message held = {};
    held.envelope.selector = MSG_ROLL_HELD;
    held.envelope.from = kGame;
    held.envelope.to = kBoard;
    Message lost = held;
    lost.envelope.selector = MSG_ROLLS_LOST;
    const TestOutbox outbox = Send(&board, {held, lost});
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(0U, NotUnderstoodCount(&board));
}

/* ---- Its statistics: counters and facts, asked for, not read from its state -------------------- */

namespace {

Message StatsQuery(RequestSeq seq)
{
    Message message = Query(seq);
    message.envelope.selector = MSG_QUERY_STATS;
    return message;
}

} // namespace

TEST(ScoreboardTest, should_answer_its_statistics_with_its_facts_and_counters)
{
    Message roll = {};
    roll.envelope.selector = MSG_ROLL;
    roll.envelope.from = kAsker;
    roll.envelope.to = kBoard;
    Scoreboard board = MakeBoard();
    const TestOutbox outbox =
        Send(&board, {FrameChanged(1, 7, true), FrameChanged(2, 12, true), roll, StatsQuery(4U)});
    ASSERT_EQ(1U, outbox.count);
    const Message &stats = outbox.items[0];
    EXPECT_EQ(MSG_STATS, stats.envelope.selector);
    EXPECT_EQ(kAsker, stats.envelope.to);
    EXPECT_EQ(kBoard, stats.envelope.from);
    EXPECT_EQ(4U, stats.envelope.seq);
    EXPECT_EQ(19U, stats.payload.stats.total);
    EXPECT_EQ(2U, stats.payload.stats.complete_frames);
    EXPECT_EQ(1U, stats.payload.stats.not_understood);
}
