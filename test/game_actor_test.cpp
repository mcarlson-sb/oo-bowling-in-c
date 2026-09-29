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
