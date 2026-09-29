/* The game actor: the pure heart of the RTOS shell. It takes one message at a time and writes
 * what it sends, replies and events, to an outbox; it knows nothing of FreeRTOS. A request's
 * reply address is opaque to it: the shell's queue, passed through untouched. */

#include <gtest/gtest.h>

#include <vector>

#include "game_actor.h"

namespace {

int s_reply_queue; /* stands in for a caller's reply queue: only its address matters */

GameActor MakeActor(ScorerVariant variant)
{
    GameActor actor;
    GameActor_Init(&actor, variant, SCORER_COUNT_PINS_DOWN);
    return actor;
}

GameMessage RollRequest(RequestSeq seq, Pins pins)
{
    GameMessage message = {};
    message.kind = GAME_MSG_ROLL;
    message.seq = seq;
    message.reply_to = &s_reply_queue;
    message.pins = pins;
    return message;
}

} // namespace

TEST(GameActorTest, should_reply_to_a_roll_with_its_sequence_number_status_and_score)
{
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    const GameMessage first = RollRequest(7U, 3U);
    const GameMessage second = RollRequest(8U, 4U);
    GameActor_Handle(&actor, &first, &outbox);
    GameActor_Handle(&actor, &second, &outbox);
    ASSERT_EQ(1U, outbox.count);
    const GameOutput &reply = outbox.items[0];
    EXPECT_EQ(GAME_OUT_REPLY, reply.kind);
    EXPECT_EQ(&s_reply_queue, reply.to);
    EXPECT_EQ(8U, reply.seq);
    EXPECT_EQ(GAME_OK, reply.status);
    EXPECT_EQ(7U, reply.score);
}

/* ---- Subscribers: a catch-up of the frames so far, then every change -------------------- */

namespace {

int s_subscriber_queue;

GameMessage SubscribeRequest(RequestSeq seq, void *subscriber)
{
    GameMessage message = {};
    message.kind = GAME_MSG_SUBSCRIBE;
    message.seq = seq;
    message.reply_to = subscriber;
    return message;
}

struct Sent {
    GameOutputKind kind;
    void *to;
    int frame;
    int score;
    bool complete;
    bool operator==(const Sent &other) const
    {
        return (kind == other.kind) && (to == other.to) && (frame == other.frame) &&
               (score == other.score) && (complete == other.complete);
    }
};

/* Frame events only; replies are checked on their own. */
std::vector<Sent> FrameEventsIn(const GameOutbox &outbox)
{
    std::vector<Sent> sent;
    for (uint8_t i = 0U; i < outbox.count; i++) {
        const GameOutput &out = outbox.items[i];
        if (out.kind == GAME_OUT_FRAME_CHANGED) {
            sent.push_back({out.kind, out.to, out.frame.frame_number, out.frame.frame_score,
                            out.frame.frame_complete});
        }
    }
    return sent;
}

void Send(GameActor *actor, const GameMessage &message, GameOutbox *outbox)
{
    GameActor_Handle(actor, &message, outbox);
}

} // namespace

TEST(GameActorTest, should_catch_a_new_subscriber_up_on_the_complete_frames_then_tell_it_changes)
{
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox); /* frame 1: 7 */

    Send(&actor, SubscribeRequest(3U, &s_subscriber_queue), &outbox);
    ASSERT_LE(1U, outbox.count);
    EXPECT_EQ(GAME_OUT_REPLY, outbox.items[0].kind); /* the reply comes first */
    EXPECT_EQ(3U, outbox.items[0].seq);
    EXPECT_EQ(GAME_OK, outbox.items[0].status);
    const Sent caught_up = {GAME_OUT_FRAME_CHANGED, &s_subscriber_queue, 1, 7, true};
    EXPECT_EQ((std::vector<Sent>{caught_up}), FrameEventsIn(outbox));

    Send(&actor, RollRequest(4U, 5U), &outbox);
    Send(&actor, RollRequest(5U, 2U), &outbox); /* frame 2: 7 */
    const Sent live = {GAME_OUT_FRAME_CHANGED, &s_subscriber_queue, 2, 7, true};
    EXPECT_EQ((std::vector<Sent>{live}), FrameEventsIn(outbox));
}

TEST(GameActorTest, should_refuse_a_subscriber_past_the_room_for_two)
{
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    int a = 0;
    int b = 0;
    int c = 0;
    Send(&actor, SubscribeRequest(1U, &a), &outbox);
    Send(&actor, SubscribeRequest(2U, &b), &outbox);
    Send(&actor, SubscribeRequest(3U, &c), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(GAME_ERR_NO_ROOM, outbox.items[0].status);
    EXPECT_EQ(&c, outbox.items[0].to); /* told, and nothing more */

    Send(&actor, RollRequest(4U, 3U), &outbox);
    Send(&actor, RollRequest(5U, 4U), &outbox);
    std::vector<void *> heard_by;
    for (const Sent &sent : FrameEventsIn(outbox)) {
        heard_by.push_back(sent.to);
    }
    EXPECT_EQ((std::vector<void *>{&a, &b}), heard_by);
}
