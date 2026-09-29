/* The game actor: the pure heart of the RTOS shell. It takes one message at a time and writes
 * what it sends, replies and events, to an outbox; it knows nothing of FreeRTOS. A request's
 * reply address is opaque to it: the shell's queue, passed through untouched. */

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

#include "game_actor.h"
#include "rules_presets.h"

namespace {

constexpr ActorId kGame = 1U;
constexpr ActorId kReplyTo = 2U; /* a caller: only its id matters here */

Message NewGameRequest(RequestSeq seq, const ScorerRules &rules)
{
    Message message = {};
    message.envelope.selector = MSG_NEW_GAME;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    message.payload.new_game.rules = rules;
    return message;
}

/* A game actor, with a game by these rules started. */
GameActor MakeActor(const ScorerRules &rules)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    GameOutbox outbox;
    const Message new_game = NewGameRequest(0U, rules);
    GameActor_Handle(&actor, &new_game, &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status) << "setup: the new game";
    return actor;
}

Message RollRequest(RequestSeq seq, Pins pins)
{
    Message message = {};
    message.envelope.selector = MSG_ROLL;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    message.payload.roll.pins = pins;
    return message;
}

} // namespace

TEST(GameActorTest, should_reply_to_a_roll_with_its_sequence_number_status_and_score)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const Message first = RollRequest(7U, 3U);
    const Message second = RollRequest(8U, 4U);
    GameActor_Handle(&actor, &first, &outbox);
    GameActor_Handle(&actor, &second, &outbox);
    ASSERT_EQ(1U, outbox.count);
    const Message &reply = outbox.items[0];
    EXPECT_EQ(MSG_REPLY, reply.envelope.selector);
    EXPECT_EQ(kReplyTo, reply.envelope.to);
    EXPECT_EQ(8U, reply.envelope.seq);
    EXPECT_EQ(GAME_OK, reply.payload.reply.status);
    EXPECT_EQ(7U, reply.payload.reply.score);
}

/* ---- Subscribers: a catch-up of the frames so far, then every change -------------------- */

namespace {

constexpr ActorId kSubscriber = 3U;

Message SubscribeRequest(RequestSeq seq, ActorId subscriber)
{
    Message message = {};
    message.envelope.selector = MSG_SUBSCRIBE;
    message.envelope.seq = seq;
    message.envelope.from = subscriber;
    return message;
}

struct Sent {
    Selector kind;
    ActorId to;
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
        const Message &out = outbox.items[i];
        if (out.envelope.selector == MSG_FRAME_CHANGED) {
            const FrameEvent &frame = out.payload.frame;
            sent.push_back({out.envelope.selector, out.envelope.to, frame.frame_number,
                            frame.frame_score, frame.frame_complete});
        }
    }
    return sent;
}

void Send(GameActor *actor, const Message &message, GameOutbox *outbox)
{
    GameActor_Handle(actor, &message, outbox);
}

} // namespace

TEST(GameActorTest, should_catch_a_new_subscriber_up_on_the_complete_frames_then_tell_it_changes)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox); /* frame 1: 7 */

    Send(&actor, SubscribeRequest(3U, kSubscriber), &outbox);
    ASSERT_LE(1U, outbox.count);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector); /* the reply comes first */
    EXPECT_EQ(3U, outbox.items[0].envelope.seq);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    const Sent caught_up = {MSG_FRAME_CHANGED, kSubscriber, 1, 7, true};
    EXPECT_EQ((std::vector<Sent>{caught_up}), FrameEventsIn(outbox));

    Send(&actor, RollRequest(4U, 5U), &outbox);
    Send(&actor, RollRequest(5U, 2U), &outbox); /* frame 2: 7 */
    const Sent live = {MSG_FRAME_CHANGED, kSubscriber, 2, 7, true};
    EXPECT_EQ((std::vector<Sent>{live}), FrameEventsIn(outbox));
}

TEST(GameActorTest, should_refuse_a_subscriber_past_the_room_for_two)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const ActorId a = 10U;
    const ActorId b = 11U;
    const ActorId c = 12U;
    Send(&actor, SubscribeRequest(1U, a), &outbox);
    Send(&actor, SubscribeRequest(2U, b), &outbox);
    Send(&actor, SubscribeRequest(3U, c), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(GAME_ERR_TOO_MANY_SUBSCRIBERS, outbox.items[0].payload.reply.status);
    EXPECT_EQ(c, outbox.items[0].envelope.to); /* told, and nothing more */

    Send(&actor, RollRequest(4U, 3U), &outbox);
    Send(&actor, RollRequest(5U, 4U), &outbox);
    std::vector<ActorId> heard_by;
    for (const Sent &sent : FrameEventsIn(outbox)) {
        heard_by.push_back(sent.to);
    }
    EXPECT_EQ((std::vector<ActorId>{a, b}), heard_by);
}

/* ---- Edits ---------------------------------------------------------------------------- */

namespace {

Message EditRequest(RequestSeq seq, RollNumber first, uint8_t removed,
                        std::initializer_list<Pins> new_pins)
{
    Message message = {};
    message.envelope.selector = MSG_EDIT;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    message.payload.edit.first_roll = first;
    message.payload.edit.rolls_removed = removed;
    for (const Pins pins : new_pins) {
        message.payload.edit.new_pins[message.payload.edit.new_count++] = pins;
    }
    return message;
}

} // namespace

TEST(GameActorTest, A23_should_tell_a_candlepin_subscriber_frame_1_changed_from_13_to_10)
{
    GameActor actor = MakeActor(rules::kCandlepin);
    GameOutbox outbox;
    RequestSeq seq = 1U;
    for (const Pins pins : std::initializer_list<Pins>{5U, 5U, 3U, 4U, 2U}) {
        Send(&actor, RollRequest(seq++, pins), &outbox);
    }
    for (int i = 0; i < 24; i++) {
        Send(&actor, RollRequest(seq++, 0U), &outbox);
    }
    Send(&actor, SubscribeRequest(seq++, kSubscriber), &outbox);
    ASSERT_LE(2U, FrameEventsIn(outbox).size());
    EXPECT_EQ((Sent{MSG_FRAME_CHANGED, kSubscriber, 1, 13, true}),
              FrameEventsIn(outbox)[0]); /* caught up: frame 1 was 13 */

    Send(&actor, EditRequest(seq++, 2U, 1U, {2U}), &outbox); /* ball 2: 5 to 2 */
    ASSERT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    EXPECT_EQ(16U, outbox.items[0].payload.reply.score);
    ASSERT_LE(1U, FrameEventsIn(outbox).size());
    EXPECT_EQ((Sent{MSG_FRAME_CHANGED, kSubscriber, 1, 10, true}),
              FrameEventsIn(outbox)[0]); /* now: frame 1 is 10 */
}

/* ---- The pinsetter's rolls: no reply, since an interrupt has nowhere to hear one -------- */

namespace {

Message PinsetterRoll(Pins pins)
{
    Message message = {};
    message.envelope.selector = MSG_PINSETTER_ROLL;
    message.payload.roll.pins = pins;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_roll_a_pinsetter_roll_into_the_game_and_tell_the_subscribers)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ(0U, outbox.count); /* frame 1 isn't complete yet, and there's no reply */
    Send(&actor, PinsetterRoll(4U), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, kSubscriber, 1, 7, true}}),
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
        const Message &out = outbox.items[i];
        if (out.envelope.selector == MSG_ROLL_HELD) {
            held.push_back({out.payload.roll_held.pins, out.payload.roll_held.position,
                            out.payload.roll_held.held, out.payload.roll_held.status});
        }
    }
    return held;
}

Message ScoreQuery(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_SCORE;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    return message;
}

Score ScoreOf(GameActor *actor)
{
    GameOutbox outbox;
    Send(actor, ScoreQuery(99U), &outbox);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    return outbox.items[0].payload.reply.score;
}

} // namespace

TEST(GameActorPinsetterTest, should_hold_an_impossible_roll_and_those_after_it_until_a_correction)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible. */
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(5U), &outbox); /* miscounted: 2 fell */
    Send(&actor, PinsetterRoll(8U), &outbox);
    EXPECT_EQ((std::vector<Held>{{8, 2, 1, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
    Send(&actor, PinsetterRoll(3U), &outbox); /* held behind the 8 */
    EXPECT_EQ((std::vector<Held>{{3, 3, 2, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
    EXPECT_EQ(0U, ScoreOf(&actor)); /* frame 1 is still open: the 8 and the 3 are held */

    Send(&actor, EditRequest(2U, 1U, 1U, {2U}), &outbox); /* ball 1: 5 to 2 */
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    /* 2 then 8, a spare, with the 3 as its bonus */
    EXPECT_EQ(13U, outbox.items[0].payload.reply.score);
    EXPECT_EQ((std::vector<Held>{}), HeldEventsIn(outbox));
}

namespace {

Message DiscardHeldRequest(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_DISCARD_HELD;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_let_the_scorer_discard_a_held_roll_that_really_was_a_glitch)
{
    /* 11 pins can't fall: this time the machine is wrong, not an earlier roll. */
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, PinsetterRoll(11U), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox);
    EXPECT_EQ(0U, ScoreOf(&actor));

    Send(&actor, DiscardHeldRequest(1U), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score); /* the 3 and the 4 went through */

    Send(&actor, DiscardHeldRequest(2U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, outbox.items[0].payload.reply.status); /* nothing is held */
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
        if (outbox.items[i].envelope.selector == MSG_ROLLS_LOST) {
            lost.push_back(outbox.items[i].payload.rolls_lost.lost);
        }
    }
    return lost;
}

Message RollsLostReport(uint16_t lost_so_far)
{
    Message message = {};
    message.envelope.selector = MSG_ROLLS_LOST;
    message.payload.rolls_lost.lost = lost_so_far;
    return message;
}

} // namespace

TEST(GameActorPinsetterTest, should_hold_pinsetter_rolls_made_after_the_game_is_over)
{
    /* Kay-oo drains these into the next game. Here there is one game, and they are held until
     * the scorer discards them: the brief's messages have no "new game". */
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(21U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ((std::vector<Held>{{3, 21, 1, GAME_ERR_GAME_OVER}}), HeldEventsIn(outbox));
    EXPECT_EQ(0U, ScoreOf(&actor));
}

TEST(GameActorPinsetterTest, should_hold_a_whole_game_of_rolls_and_count_any_past_that_lost)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(21U, kSubscriber), &outbox);
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
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, RollsLostReport(2U), &outbox);
    EXPECT_EQ((std::vector<int>{2}), LostEventsIn(outbox));
    Send(&actor, RollsLostReport(2U), &outbox); /* nothing new */
    EXPECT_EQ((std::vector<int>{}), LostEventsIn(outbox));
    Send(&actor, RollsLostReport(5U), &outbox);
    EXPECT_EQ((std::vector<int>{5}), LostEventsIn(outbox));
}

namespace {

Message UnsubscribeRequest(RequestSeq seq, ActorId subscriber)
{
    Message message = SubscribeRequest(seq, subscriber);
    message.envelope.selector = MSG_UNSUBSCRIBE;
    return message;
}

} // namespace

TEST(GameActorTest, should_stop_telling_a_subscriber_that_unsubscribes)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const ActorId a = 10U;
    const ActorId b = 11U;
    Send(&actor, SubscribeRequest(1U, a), &outbox);
    Send(&actor, SubscribeRequest(2U, b), &outbox);
    Send(&actor, UnsubscribeRequest(3U, a), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(a, outbox.items[0].envelope.to);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);

    Send(&actor, RollRequest(4U, 3U), &outbox);
    Send(&actor, RollRequest(5U, 4U), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, b, 1, 7, true}}), FrameEventsIn(outbox));

    Send(&actor, UnsubscribeRequest(6U, a), &outbox);
    EXPECT_EQ(GAME_ERR_NOT_SUBSCRIBED, outbox.items[0].payload.reply.status);
    Send(&actor, SubscribeRequest(7U, a), &outbox); /* its room is free again */
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
}

/* ---- Pinned by mutation testing: each kills an actor mutant that survived ------------------ */

TEST(GameActorTest, should_tell_a_subscriber_every_frame_one_roll_completes_in_order)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, RollRequest(2U, 10U), &outbox);
    Send(&actor, RollRequest(3U, 3U), &outbox);
    Send(&actor, RollRequest(4U, 4U), &outbox); /* completes the strike and its own frame */
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, kSubscriber, 1, 17, true},
                                 {MSG_FRAME_CHANGED, kSubscriber, 2, 7, true}}),
              FrameEventsIn(outbox));
}

TEST(GameActorPinsetterTest, should_tell_every_subscriber_about_held_and_lost_rolls)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const ActorId a = 10U;
    const ActorId b = 11U;
    Send(&actor, SubscribeRequest(1U, a), &outbox);
    Send(&actor, SubscribeRequest(2U, b), &outbox);
    Send(&actor, PinsetterRoll(11U), &outbox);
    std::vector<ActorId> told;
    for (uint8_t i = 0U; i < outbox.count; i++) {
        told.push_back(outbox.items[i].envelope.to);
    }
    EXPECT_EQ((std::vector<ActorId>{a, b}), told); /* both hear the roll held */
    Send(&actor, RollsLostReport(1U), &outbox);
    EXPECT_EQ(2U, outbox.count); /* both hear the roll lost */
    EXPECT_EQ(b, outbox.items[1].envelope.to);
}

TEST(GameActorTest, should_stop_telling_the_second_subscriber_when_it_unsubscribes)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const ActorId a = 10U;
    const ActorId b = 11U;
    Send(&actor, SubscribeRequest(1U, a), &outbox);
    Send(&actor, SubscribeRequest(2U, b), &outbox);
    Send(&actor, UnsubscribeRequest(3U, b), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(4U, 3U), &outbox);
    Send(&actor, RollRequest(5U, 4U), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, a, 1, 7, true}}), FrameEventsIn(outbox));
}

TEST(GameActorPinsetterTest, should_tell_the_frames_the_held_rolls_complete_when_they_go_through)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(5U), &outbox); /* miscounted: 2 fell */
    Send(&actor, PinsetterRoll(8U), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, EditRequest(2U, 1U, 1U, {2U}), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, kSubscriber, 1, 13, true}}),
              FrameEventsIn(outbox)); /* the spare, completed by the held 3 */
}

TEST(GameActorPinsetterTest, should_hold_again_at_the_next_held_roll_the_game_rejects)
{
    /* A correction lets the 8 through, but the 11 behind it can never fall: the held rolls stop
     * there again, and the subscribers are told what is held now, and why. */
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(5U), &outbox); /* miscounted: 2 fell */
    Send(&actor, PinsetterRoll(8U), &outbox);
    Send(&actor, PinsetterRoll(11U), &outbox);
    Send(&actor, EditRequest(2U, 1U, 1U, {2U}), &outbox);
    EXPECT_EQ((std::vector<Held>{{11, 3, 1, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
}

TEST(GameActorPinsetterTest, should_let_the_rest_through_after_discarding_from_a_full_held_list)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    for (int i = 0; i < 30; i++) {
        Send(&actor, PinsetterRoll(1U), &outbox); /* a full held list, all after the game */
    }
    Send(&actor, DiscardHeldRequest(21U), &outbox);
    /* the game is still over: 29 remain held */
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
}

TEST(GameActorTest, should_refuse_unsubscribing_the_same_subscriber_twice)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    const ActorId a = 10U;
    const ActorId b = 11U;
    Send(&actor, SubscribeRequest(1U, a), &outbox);
    Send(&actor, SubscribeRequest(2U, b), &outbox);
    Send(&actor, UnsubscribeRequest(3U, b), &outbox);
    Send(&actor, UnsubscribeRequest(4U, b), &outbox);
    EXPECT_EQ(GAME_ERR_NOT_SUBSCRIBED, outbox.items[0].payload.reply.status);
}

/* ---- The outbox holds the most one message can send ------------------------------------ */

TEST(GameActorTest, should_fill_the_outbox_exactly_with_the_most_one_message_can_send)
{
    /* The worst case: an edit that reopens all ten frames, then lets through held rolls that
     * complete all ten again, then holds the next one again, told to two subscribers. */
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    for (int i = 0; i < 12; i++) {
        Send(&actor, RollRequest(static_cast<RequestSeq>(i + 1), 10U), &outbox);
    }
    const ActorId a = 10U;
    const ActorId b = 11U;
    Send(&actor, SubscribeRequest(20U, a), &outbox);
    Send(&actor, SubscribeRequest(21U, b), &outbox);
    for (int i = 0; i < 13; i++) {
        Send(&actor, PinsetterRoll(10U), &outbox); /* held: the game is over */
    }
    Send(&actor, EditRequest(22U, 1U, 12U, {}), &outbox); /* every ball out */
    EXPECT_EQ(GAME_OUTBOX_CAPACITY, outbox.count);
}

/* ---- Pinned at the phase 2 stop, from mutation testing and coverage --------------------- */

TEST(GameActorPinsetterTest, should_tell_the_new_reason_when_a_held_roll_is_refused_for_another)
{
    /* Held because the game was over; an edit reopens the tenth frame, and the replay then
     * refuses the second 6 for too many pins: the subscribers hear the new reason. */
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, SubscribeRequest(21U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(6U), &outbox);
    Send(&actor, PinsetterRoll(6U), &outbox);
    Send(&actor, EditRequest(22U, 19U, 2U, {}), &outbox); /* balls 19 and 20 out */
    EXPECT_EQ((std::vector<Held>{{6, 20, 1, GAME_ERR_INVALID_PINS}}), HeldEventsIn(outbox));
}

TEST(GameActorTest, should_start_a_fresh_game_with_nothing_held_when_initialized_again)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    GameOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox); /* held: the game is over */
    GameActor_Init(&actor, kGame);
    Send(&actor, NewGameRequest(0U, rules::kTenPin), &outbox);
    Send(&actor, DiscardHeldRequest(1U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, outbox.items[0].payload.reply.status);
}

TEST(GameActorTest, should_catch_a_subscriber_joining_mid_frame_up_on_the_complete_frames_only)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, RollRequest(3U, 5U), &outbox); /* frame 2 started, not complete */
    Send(&actor, SubscribeRequest(4U, kSubscriber), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, kSubscriber, 1, 7, true}}),
              FrameEventsIn(outbox));
}

/* ---- A selector the game doesn't answer ------------------------------------------------ */

TEST(GameActorTest, should_reply_not_understood_to_a_selector_it_does_not_answer_and_count_it)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Message frame_changed = {};
    frame_changed.envelope.selector = MSG_FRAME_CHANGED;
    frame_changed.envelope.from = kReplyTo;
    frame_changed.envelope.to = kGame;
    frame_changed.envelope.seq = 5U;
    Send(&actor, frame_changed, &outbox);
    ASSERT_EQ(1U, outbox.count);
    const Message &reply = outbox.items[0];
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, reply.envelope.selector);
    EXPECT_EQ(kReplyTo, reply.envelope.to);
    EXPECT_EQ(kGame, reply.envelope.from);
    EXPECT_EQ(5U, reply.envelope.seq);
    EXPECT_EQ(MSG_FRAME_CHANGED, reply.payload.not_understood.selector);
    EXPECT_EQ(1U, actor.not_understood);
}

TEST(GameActorTest, should_count_but_never_answer_a_not_understood_so_two_kinds_cannot_echo_it)
{
    /* Two kinds that don't understand each other would otherwise trade NOT_UNDERSTOODs
     * forever. The same for a message from no one, such as the pinsetter's. */
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Message not_understood = {};
    not_understood.envelope.selector = MSG_NOT_UNDERSTOOD;
    not_understood.envelope.from = kReplyTo;
    not_understood.envelope.to = kGame;
    Send(&actor, not_understood, &outbox);
    EXPECT_EQ(0U, outbox.count);
    Message from_no_one = {};
    from_no_one.envelope.selector = MSG_REPLY;
    from_no_one.envelope.from = ACTOR_ID_NONE;
    from_no_one.envelope.to = kGame;
    Send(&actor, from_no_one, &outbox);
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(2U, actor.not_understood);
}

/* ---- A game whose rules arrive in a message ------------------------------------------ */

TEST(GameActorLifecycleTest, should_start_a_game_by_the_rules_a_new_game_carries)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    GameOutbox outbox;
    Send(&actor, NewGameRequest(9U, rules::kCandlepin), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(9U, outbox.items[0].envelope.seq);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(10U, 3U), &outbox);
    Send(&actor, RollRequest(11U, 3U), &outbox);
    Send(&actor, RollRequest(12U, 3U), &outbox); /* candlepin: a frame takes three balls */
    EXPECT_EQ(9U, outbox.items[0].payload.reply.score);
}

TEST(GameActorLifecycleTest, should_reply_no_game_to_anything_but_a_new_game_before_one_starts)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    GameOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
    Send(&actor, SubscribeRequest(2U, kSubscriber), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
    Send(&actor, ScoreQuery(3U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
}

TEST(GameActorLifecycleTest, should_refuse_a_new_game_by_rules_the_scorer_cannot_play)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    GameOutbox outbox;
    const ScorerRules no_frames = {0U, 2U, 10U, {2U, 1U, 0U}, 0U};
    Send(&actor, NewGameRequest(1U, no_frames), &outbox);
    EXPECT_EQ(GAME_ERR_INVALID_RULES, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(2U, 3U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status); /* still no game */
}

TEST(GameActorLifecycleTest, should_refuse_a_new_game_while_one_is_in_play_and_keep_it)
{
    GameActor actor = MakeActor(rules::kTenPin);
    GameOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, NewGameRequest(3U, rules::kCandlepin), &outbox);
    EXPECT_EQ(GAME_ERR_GAME_IN_PROGRESS, outbox.items[0].payload.reply.status);
    EXPECT_EQ(7U, ScoreOf(&actor));
}
