/* The running-average kind: a subscriber that answers QUERY_SCORE with the average of the frames
 * it has heard complete. The same selectors as the scoreboard, a different answer. */

#include <gtest/gtest.h>

#include "test_outbox.h"

#include <cstring>

#include <initializer_list>

extern "C" {
#include "running_average.h"
#include "running_average_state.h"
}

namespace {

constexpr ActorId kGame = 1U;
constexpr ActorId kAverage = 5U;
constexpr ActorId kAsker = 4U;

RunningAverage MakeAverage()
{
    RunningAverage average;
    RunningAverage_Init(&average, kAverage);
    return average;
}

Message FrameChanged(int frame, int score, bool complete)
{
    Message message = {};
    message.envelope.selector = MSG_FRAME_CHANGED;
    message.envelope.from = kGame;
    message.envelope.to = kAverage;
    message.payload.frame = {static_cast<FrameNumber>(frame), static_cast<Score>(score), complete};
    return message;
}

Message Query(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_SCORE;
    message.envelope.from = kAsker;
    message.envelope.to = kAverage;
    message.envelope.seq = seq;
    return message;
}

TestOutbox Send(RunningAverage *average, std::initializer_list<Message> messages)
{
    TestOutbox outbox(RUNNING_AVERAGE_MOST_SENT);
    for (const Message &message : messages) {
        outbox.count = 0U;
        RunningAverage_Handle(average, &message, &outbox);
    }
    return outbox;
}

} // namespace

TEST(RunningAverageTest, should_answer_the_average_of_the_frames_it_has_heard_complete)
{
    RunningAverage average = MakeAverage();
    const TestOutbox outbox = Send(&average, {FrameChanged(1, 7, true), FrameChanged(2, 12, true),
                                          FrameChanged(3, 0, false), Query(5U)});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(kAsker, outbox.items[0].envelope.to);
    EXPECT_EQ(5U, outbox.items[0].envelope.seq);
    EXPECT_EQ(9U, outbox.items[0].payload.reply.score); /* 19 over 2 frames, rounded down */
}

TEST(RunningAverageTest, should_answer_0_before_any_frame_is_complete)
{
    RunningAverage average = MakeAverage();
    const TestOutbox outbox = Send(&average, {FrameChanged(1, 0, false), Query(1U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}

TEST(RunningAverageTest, should_not_understand_a_roll_and_count_it)
{
    RunningAverage average = MakeAverage();
    Message roll = {};
    roll.envelope.selector = MSG_ROLL;
    roll.envelope.from = kAsker;
    roll.envelope.to = kAverage;
    const TestOutbox outbox = Send(&average, {roll});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, outbox.items[0].envelope.selector);
    EXPECT_EQ(1U, average.not_understood);
}

TEST(RunningAverageTest, should_take_the_reply_to_its_subscription_in_silence)
{
    RunningAverage average = MakeAverage();
    Message reply = {};
    reply.envelope.selector = MSG_REPLY;
    reply.envelope.from = kGame;
    reply.envelope.to = kAverage;
    const TestOutbox outbox = Send(&average, {reply});
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(0U, average.not_understood);
}

TEST(RunningAverageTest, should_start_empty_whatever_memory_it_is_given)
{
    RunningAverage average;
    std::memset(&average, 0xFF, sizeof(average));
    RunningAverage_Init(&average, kAverage);
    const TestOutbox outbox = Send(&average, {FrameChanged(1, 8, true), Query(1U)});
    EXPECT_EQ(8U, outbox.items[0].payload.reply.score); /* one frame: nothing left over */
    EXPECT_EQ(0U, average.not_understood);
}

TEST(RunningAverageTest, should_say_not_understood_from_its_own_id)
{
    RunningAverage average = MakeAverage();
    Message roll = {};
    roll.envelope.selector = MSG_ROLL;
    roll.envelope.from = kAsker;
    roll.envelope.to = kAverage;
    const TestOutbox outbox = Send(&average, {roll});
    EXPECT_EQ(kAverage, outbox.items[0].envelope.from);
}

TEST(RunningAverageTest, should_take_the_rest_of_what_a_game_tells_subscribers_in_silence)
{
    RunningAverage average = MakeAverage();
    Message held = {};
    held.envelope.selector = MSG_ROLL_HELD;
    held.envelope.from = kGame;
    held.envelope.to = kAverage;
    Message lost = held;
    lost.envelope.selector = MSG_ROLLS_LOST;
    const TestOutbox outbox = Send(&average, {held, lost});
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(0U, average.not_understood);
}

TEST(RunningAverageTest, should_answer_its_statistics_with_the_facts_behind_its_average)
{
    /* 19 over 2 frames: the answer rounds to 9, the facts don't. */
    Message ask = Query(4U);
    ask.envelope.selector = MSG_QUERY_STATS;
    RunningAverage average = MakeAverage();
    const TestOutbox outbox =
        Send(&average, {FrameChanged(1, 7, true), FrameChanged(2, 12, true), ask});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_STATS, outbox.items[0].envelope.selector);
    EXPECT_EQ(4U, outbox.items[0].envelope.seq);
    EXPECT_EQ(19U, outbox.items[0].payload.stats.total);
    EXPECT_EQ(2U, outbox.items[0].payload.stats.complete_frames);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.not_understood);
}
