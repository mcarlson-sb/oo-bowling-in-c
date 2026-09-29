/* The game actor: the pure heart of the RTOS shell. It takes one message at a time and writes
 * what it sends, replies and events, to an outbox; it knows nothing of FreeRTOS. A request's
 * reply address is opaque to it: the shell's queue, passed through untouched. */

#include <gtest/gtest.h>

#include <initializer_list>
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

/* ---- Edits ---------------------------------------------------------------------------- */

namespace {

GameMessage EditRequest(RequestSeq seq, RollNumber first, uint8_t removed,
                        std::initializer_list<Pins> new_pins)
{
    GameMessage message = {};
    message.kind = GAME_MSG_EDIT;
    message.seq = seq;
    message.reply_to = &s_reply_queue;
    message.first_roll = first;
    message.rolls_removed = removed;
    for (const Pins pins : new_pins) {
        message.new_pins[message.new_count++] = pins;
    }
    return message;
}

} // namespace

TEST(GameActorTest, A23_should_tell_a_candlepin_subscriber_frame_1_changed_from_13_to_10)
{
    GameActor actor = MakeActor(SCORER_CANDLEPIN);
    GameOutbox outbox;
    RequestSeq seq = 1U;
    for (const Pins pins : std::initializer_list<Pins>{5U, 5U, 3U, 4U, 2U}) {
        Send(&actor, RollRequest(seq++, pins), &outbox);
    }
    for (int i = 0; i < 24; i++) {
        Send(&actor, RollRequest(seq++, 0U), &outbox);
    }
    Send(&actor, SubscribeRequest(seq++, &s_subscriber_queue), &outbox);
    ASSERT_LE(2U, FrameEventsIn(outbox).size());
    EXPECT_EQ((Sent{GAME_OUT_FRAME_CHANGED, &s_subscriber_queue, 1, 13, true}),
              FrameEventsIn(outbox)[0]); /* caught up: frame 1 was 13 */

    Send(&actor, EditRequest(seq++, 2U, 1U, {2U}), &outbox); /* ball 2: 5 to 2 */
    ASSERT_EQ(GAME_OUT_REPLY, outbox.items[0].kind);
    EXPECT_EQ(GAME_OK, outbox.items[0].status);
    EXPECT_EQ(16U, outbox.items[0].score);
    ASSERT_LE(1U, FrameEventsIn(outbox).size());
    EXPECT_EQ((Sent{GAME_OUT_FRAME_CHANGED, &s_subscriber_queue, 1, 10, true}),
              FrameEventsIn(outbox)[0]); /* now: frame 1 is 10 */
}

/* ---- The pinsetter's rolls: no reply, since an interrupt has nowhere to hear one -------- */

namespace {

GameMessage PinsetterRoll(Pins pins)
{
    GameMessage message = {};
    message.kind = GAME_MSG_PINSETTER_ROLL;
    message.pins = pins;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_roll_a_pinsetter_roll_into_the_game_and_tell_the_subscribers)
{
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, &s_subscriber_queue), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ(0U, outbox.count); /* frame 1 isn't complete yet, and there's no reply */
    Send(&actor, PinsetterRoll(4U), &outbox);
    EXPECT_EQ((std::vector<Sent>{{GAME_OUT_FRAME_CHANGED, &s_subscriber_queue, 1, 7, true}}),
              FrameEventsIn(outbox));
    EXPECT_EQ(1U, outbox.count);
}

/* ---- A pinsetter roll the game rejects is held, with every one after it -------------------
 * Kay-oo's pinsetter stops its drain at the roll and keeps it until the scorer resolves it
 * (pinsetter_test.cpp on kay-oo); here the actor holds it, so the pinsetter's queue never
 * backs up. These tests mirror kay-oo's, to compare the two. */

namespace {

struct Held {
    int pins;
    int position;
    int held;
    GameStatus status;
    bool operator==(const Held &other) const
    {
        return (pins == other.pins) && (position == other.position) && (held == other.held) &&
               (status == other.status);
    }
};

std::vector<Held> HeldEventsIn(const GameOutbox &outbox)
{
    std::vector<Held> held;
    for (uint8_t i = 0U; i < outbox.count; i++) {
        const GameOutput &out = outbox.items[i];
        if (out.kind == GAME_OUT_ROLL_HELD) {
            held.push_back({out.pins, out.position, out.held, out.status});
        }
    }
    return held;
}

GameMessage ScoreQuery(RequestSeq seq)
{
    GameMessage message = {};
    message.kind = GAME_MSG_QUERY_SCORE;
    message.seq = seq;
    message.reply_to = &s_reply_queue;
    return message;
}

Score ScoreOf(GameActor *actor)
{
    GameOutbox outbox;
    Send(actor, ScoreQuery(99U), &outbox);
    EXPECT_EQ(GAME_OUT_REPLY, outbox.items[0].kind);
    return outbox.items[0].score;
}

} // namespace

TEST(GameActorPinsetterTest, should_hold_an_impossible_roll_and_those_after_it_until_a_correction)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible. */
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, &s_subscriber_queue), &outbox);
    Send(&actor, PinsetterRoll(5U), &outbox); /* miscounted: 2 fell */
    Send(&actor, PinsetterRoll(8U), &outbox);
    EXPECT_EQ((std::vector<Held>{{8, 2, 1, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
    Send(&actor, PinsetterRoll(3U), &outbox); /* held behind the 8 */
    EXPECT_EQ((std::vector<Held>{{3, 3, 2, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
    EXPECT_EQ(0U, ScoreOf(&actor)); /* frame 1 is still open: the 8 and the 3 are held */

    Send(&actor, EditRequest(2U, 1U, 1U, {2U}), &outbox); /* ball 1: 5 to 2 */
    EXPECT_EQ(GAME_OK, outbox.items[0].status);
    EXPECT_EQ(13U, outbox.items[0].score); /* 2 then 8, a spare, with the 3 as its bonus */
    EXPECT_EQ((std::vector<Held>{}), HeldEventsIn(outbox));
}

namespace {

GameMessage DiscardHeldRequest(RequestSeq seq)
{
    GameMessage message = {};
    message.kind = GAME_MSG_DISCARD_HELD;
    message.seq = seq;
    message.reply_to = &s_reply_queue;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_let_the_scorer_discard_a_held_roll_that_really_was_a_glitch)
{
    /* 11 pins can't fall: this time the machine is wrong, not an earlier roll. */
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    Send(&actor, PinsetterRoll(11U), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox);
    EXPECT_EQ(0U, ScoreOf(&actor));

    Send(&actor, DiscardHeldRequest(1U), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].status);
    EXPECT_EQ(7U, outbox.items[0].score); /* the 3 and the 4 went through */

    Send(&actor, DiscardHeldRequest(2U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, outbox.items[0].status); /* nothing is held */
}

namespace {

void BowlAGutterGame(GameActor *actor)
{
    GameOutbox outbox;
    for (int i = 0; i < 20; i++) {
        Send(actor, RollRequest(static_cast<RequestSeq>(i + 1), 0U), &outbox);
    }
}

std::vector<int> LostEventsIn(const GameOutbox &outbox)
{
    std::vector<int> lost;
    for (uint8_t i = 0U; i < outbox.count; i++) {
        if (outbox.items[i].kind == GAME_OUT_ROLLS_LOST) {
            lost.push_back(outbox.items[i].lost);
        }
    }
    return lost;
}

GameMessage RollsLostReport(uint16_t lost_so_far)
{
    GameMessage message = {};
    message.kind = GAME_MSG_ROLLS_LOST;
    message.lost = lost_so_far;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_hold_pinsetter_rolls_made_after_the_game_is_over)
{
    /* Kay-oo drains these into the next game. Here there is one game, and they are held until
     * the scorer discards them: the brief's messages have no "new game". */
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(21U, &s_subscriber_queue), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ((std::vector<Held>{{3, 21, 1, GAME_ERR_GAME_OVER}}), HeldEventsIn(outbox));
    EXPECT_EQ(0U, ScoreOf(&actor));
}

TEST(GameActorPinsetterTest, should_hold_a_whole_game_of_rolls_and_count_any_past_that_lost)
{
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(21U, &s_subscriber_queue), &outbox);
    for (int i = 0; i < 30; i++) {
        Send(&actor, PinsetterRoll(1U), &outbox);
        EXPECT_EQ((std::vector<int>{}), LostEventsIn(outbox)) << "roll " << (i + 1);
    }
    Send(&actor, PinsetterRoll(1U), &outbox); /* the 31st: no room */
    EXPECT_EQ((std::vector<int>{1}), LostEventsIn(outbox));
    EXPECT_EQ((std::vector<Held>{}), HeldEventsIn(outbox));
}

TEST(GameActorPinsetterTest, should_tell_the_subscribers_the_total_of_rolls_lost_everywhere)
{
    /* The pinsetter's queue reports its own count; the actor adds the rolls it had no room to
     * hold, and tells the total. */
    GameActor actor = MakeActor(SCORER_TEN_PIN);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, &s_subscriber_queue), &outbox);
    Send(&actor, RollsLostReport(2U), &outbox);
    EXPECT_EQ((std::vector<int>{2}), LostEventsIn(outbox));
    Send(&actor, RollsLostReport(2U), &outbox); /* nothing new */
    EXPECT_EQ((std::vector<int>{}), LostEventsIn(outbox));
    Send(&actor, RollsLostReport(5U), &outbox);
    EXPECT_EQ((std::vector<int>{5}), LostEventsIn(outbox));
}
