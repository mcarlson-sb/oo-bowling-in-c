/* The running-average kind: a subscriber that answers QUERY_SCORE with the average of the frames
 * it has heard complete. The same selectors as the scoreboard, a different answer. */

#include <gtest/gtest.h>

#include <initializer_list>

extern "C" {
#include "running_average.h"
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

Outbox Send(RunningAverage *average, std::initializer_list<Message> messages)
{
    Outbox outbox;
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
    const Outbox outbox = Send(&average, {FrameChanged(1, 7, true), FrameChanged(2, 12, true),
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
    const Outbox outbox = Send(&average, {FrameChanged(1, 0, false), Query(1U)});
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
}

TEST(RunningAverageTest, should_not_understand_a_roll_and_count_it)
{
    RunningAverage average = MakeAverage();
    Message roll = {};
    roll.envelope.selector = MSG_ROLL;
    roll.envelope.from = kAsker;
    roll.envelope.to = kAverage;
    const Outbox outbox = Send(&average, {roll});
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, outbox.items[0].envelope.selector);
    EXPECT_EQ(1U, average.not_understood);
}
