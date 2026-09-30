/* The game actor: the pure heart of the RTOS shell. It takes one message at a time and writes
 * what it sends, replies and events, to an outbox; it knows nothing of FreeRTOS. A request's
 * reply address is opaque to it: the shell's queue, passed through untouched. */

#include <gtest/gtest.h>

#include "test_outbox.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game_actor.h"
#include "game_actor_state.h"
#include "rules_presets.h"

static_assert(TestOutbox::kMostAnyKindSends == GAME_OUTBOX_CAPACITY,
              "a test outbox holds the most one message makes the game send");

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
    TestOutbox outbox;
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
    TestOutbox outbox;
    const Message first = RollRequest(7U, 3U);
    const Message second = RollRequest(8U, 4U);
    GameActor_Handle(&actor, &first, &outbox);
    outbox.count = 0U; /* as the host does, once it has posted them */
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
std::vector<Sent> FrameEventsIn(const Outbox &outbox)
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

/* As the host delivers it: into an empty outbox. */
void Send(GameActor *actor, const Message &message, Outbox *outbox)
{
    outbox->count = 0U;
    GameActor_Handle(actor, &message, outbox);
}

} // namespace

TEST(GameActorTest, should_catch_a_new_subscriber_up_on_the_complete_frames_then_tell_it_changes)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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

std::vector<Held> HeldEventsIn(const Outbox &outbox)
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

Message FigureQuery(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_FIGURE;
    message.envelope.seq = seq;
    message.envelope.from = kReplyTo;
    return message;
}

Score ScoreOf(GameActor *actor)
{
    TestOutbox outbox;
    Send(actor, FigureQuery(99U), &outbox);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    return outbox.items[0].payload.reply.score;
}

} // namespace

TEST(GameActorPinsetterTest, should_hold_an_impossible_roll_and_those_after_it_until_a_correction)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible. */
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
    for (int i = 0; i < 20; i++) {
        Send(actor, RollRequest(static_cast<RequestSeq>(i + 1), 0U), &outbox);
    }
}

std::vector<int> LostEventsIn(const Outbox &outbox)
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
    TestOutbox outbox;
    Send(&actor, SubscribeRequest(21U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ((std::vector<Held>{{3, 21, 1, GAME_ERR_GAME_OVER}}), HeldEventsIn(outbox));
    EXPECT_EQ(0U, ScoreOf(&actor));
}

TEST(GameActorPinsetterTest, should_hold_a_whole_game_of_rolls_and_count_any_past_that_lost)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
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
    TestOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox); /* held: the game is over */
    GameActor_Init(&actor, kGame);
    Send(&actor, NewGameRequest(0U, rules::kTenPin), &outbox);
    Send(&actor, DiscardHeldRequest(1U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, outbox.items[0].payload.reply.status);
}

TEST(GameActorTest, should_catch_a_subscriber_joining_mid_frame_up_on_the_complete_frames_only)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, RollRequest(3U, 5U), &outbox); /* frame 2 started, not complete */
    Send(&actor, SubscribeRequest(4U, kSubscriber), &outbox);
    EXPECT_EQ((std::vector<Sent>{{MSG_FRAME_CHANGED, kSubscriber, 1, 7, true}}),
              FrameEventsIn(outbox));
}

namespace {

/* Its not-understood count, asked for rather than read from its state. */
uint16_t NotUnderstoodCount(GameActor *actor)
{
    Message ask = FigureQuery(99U);
    ask.envelope.selector = MSG_QUERY_STATS;
    TestOutbox outbox;
    Send(actor, ask, &outbox);
    EXPECT_EQ(MSG_STATS, outbox.items[0].envelope.selector);
    return outbox.items[0].payload.stats.not_understood;
}

} // namespace

/* ---- A selector the game doesn't answer ------------------------------------------------ */

TEST(GameActorTest, should_count_an_event_it_does_not_listen_to_and_never_answer_it)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Message frame_changed = {};
    frame_changed.envelope.selector = MSG_FRAME_CHANGED;
    frame_changed.envelope.from = kReplyTo;
    frame_changed.envelope.to = kGame;
    Send(&actor, frame_changed, &outbox);
    EXPECT_EQ(0U, outbox.count);
    EXPECT_EQ(1U, NotUnderstoodCount(&actor));
}

TEST(GameActorTest, should_count_but_never_answer_a_not_understood_so_two_kinds_cannot_echo_it)
{
    /* Two kinds that don't understand each other would otherwise trade NOT_UNDERSTOODs
     * forever. The same for a message from no one, such as the pinsetter's. */
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
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
    EXPECT_EQ(2U, NotUnderstoodCount(&actor));
}

/* ---- A game whose rules arrive in a message ------------------------------------------ */

TEST(GameActorLifecycleTest, should_start_a_game_by_the_rules_a_new_game_carries)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
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

TEST(GameActorLifecycleTest, should_reply_no_game_to_anything_but_a_new_game_or_a_stats_query_before_one_starts)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    ASSERT_EQ(1U, outbox.count);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
    Send(&actor, SubscribeRequest(2U, kSubscriber), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
    Send(&actor, FigureQuery(3U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status);
}

TEST(GameActorLifecycleTest, should_refuse_a_new_game_by_rules_the_scorer_cannot_play)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    const ScorerRules no_frames = {0U, 2U, 10U, {2U, 1U, 0U}, 0U};
    Send(&actor, NewGameRequest(1U, no_frames), &outbox);
    EXPECT_EQ(GAME_ERR_INVALID_RULES, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(2U, 3U), &outbox);
    EXPECT_EQ(GAME_ERR_NO_GAME, outbox.items[0].payload.reply.status); /* still no game */
}

TEST(GameActorLifecycleTest, should_refuse_a_new_game_while_one_is_in_play_and_keep_it)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, NewGameRequest(3U, rules::kCandlepin), &outbox);
    EXPECT_EQ(GAME_ERR_GAME_IN_PROGRESS, outbox.items[0].payload.reply.status);
    EXPECT_EQ(7U, ScoreOf(&actor));
}

TEST(GameActorLifecycleTest, should_start_a_new_game_after_one_ends)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    Send(&actor, NewGameRequest(21U, rules::kCandlepin), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(22U, 3U), &outbox);
    Send(&actor, RollRequest(23U, 3U), &outbox);
    Send(&actor, RollRequest(24U, 3U), &outbox);
    EXPECT_EQ(9U, outbox.items[0].payload.reply.score); /* candlepin's three balls a frame */
}

TEST(GameActorLifecycleTest, should_play_the_rolls_held_after_a_game_into_the_new_one)
{
    /* The pinsetter counted the next game's first frame before anyone started it. */
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    Send(&actor, SubscribeRequest(21U, kSubscriber), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox); /* both held: the game is over */
    Send(&actor, NewGameRequest(22U, rules::kTenPin), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score);
    const Sent frame_1 = {MSG_FRAME_CHANGED, kSubscriber, 1, 7, true};
    const std::vector<Sent> heard = FrameEventsIn(outbox);
    EXPECT_NE(heard.end(), std::find(heard.begin(), heard.end(), frame_1));
}

TEST(GameActorLifecycleTest, should_hold_the_pinsetters_rolls_before_any_game_and_play_them_into_it)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ(0U, outbox.count); /* from no one: no reply, and no one subscribed yet */
    Send(&actor, PinsetterRoll(4U), &outbox);
    Send(&actor, NewGameRequest(1U, rules::kTenPin), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status);
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score);
}

TEST(GameActorLifecycleTest, should_tell_subscribers_every_frame_of_the_old_game_reopened)
{
    /* A scoreboard kept up by the events would otherwise show the last game's frames. */
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    BowlAGutterGame(&actor);
    Send(&actor, NewGameRequest(21U, rules::kTenPin), &outbox);
    std::vector<Sent> reopened;
    for (int frame = 1; frame <= 10; ++frame) {
        reopened.push_back({MSG_FRAME_CHANGED, kSubscriber, frame, 0, false});
    }
    EXPECT_EQ(reopened, FrameEventsIn(outbox));
}

/* ---- Pinned at the phase 3 interim stop, from mutation testing and coverage --------------- */

TEST(GameActorTest, should_not_understand_a_selector_past_the_protocols_end)
{
    /* An id is a byte and so is nothing else, but a selector is an enum any sender can fill in. */
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    for (const int past : {static_cast<int>(MSG_SELECTOR_COUNT), 200}) {
        Message nonsense = {};
        nonsense.envelope.selector = static_cast<Selector>(past);
        nonsense.envelope.from = kReplyTo;
        Send(&actor, nonsense, &outbox);
        ASSERT_EQ(1U, outbox.count) << "selector " << past;
        EXPECT_EQ(MSG_NOT_UNDERSTOOD, outbox.items[0].envelope.selector) << "selector " << past;
    }
    EXPECT_EQ(2U, NotUnderstoodCount(&actor));
}

TEST(GameActorLifecycleTest, should_keep_the_pinsetters_lost_count_from_before_any_game)
{
    /* Reported as a running total: heard again once the game starts, it is no news. */
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, RollsLostReport(2U), &outbox);
    Send(&actor, NewGameRequest(1U, rules::kTenPin), &outbox);
    Send(&actor, SubscribeRequest(2U, kSubscriber), &outbox);
    Send(&actor, RollsLostReport(2U), &outbox);
    EXPECT_EQ((std::vector<int>{}), LostEventsIn(outbox));
}

/* ---- Its statistics: counters and facts, asked for, not read from its state -------------------- */

namespace {

Message StatsQuery(RequestSeq seq)
{
    Message message = FigureQuery(seq);
    message.envelope.selector = MSG_QUERY_STATS;
    return message;
}

} // namespace

TEST(GameActorTest, should_answer_its_statistics_with_its_counters_and_facts)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox);  /* held: the game is over */
    Send(&actor, RollsLostReport(2U), &outbox); /* the pinsetter lost two */
    Message frame_changed = {};
    frame_changed.envelope.selector = MSG_FRAME_CHANGED;
    frame_changed.envelope.from = kReplyTo;
    Send(&actor, frame_changed, &outbox);       /* not understood */
    Send(&actor, StatsQuery(9U), &outbox);
    ASSERT_EQ(1U, outbox.count);
    const Message &stats = outbox.items[0];
    EXPECT_EQ(MSG_STATS, stats.envelope.selector);
    EXPECT_EQ(9U, stats.envelope.seq);
    EXPECT_EQ(1U, stats.payload.stats.not_understood);
    EXPECT_EQ(2U, stats.payload.stats.rolls_lost);
    EXPECT_EQ(1U, stats.payload.stats.rolls_held);
    EXPECT_EQ(10U, stats.payload.stats.complete_frames);
    EXPECT_EQ(0U, stats.payload.stats.total);
}

TEST(GameActorTest, should_count_in_its_statistics_the_rolls_lost_to_the_pinsetter_and_to_a_full_held_list)
{
    /* Pinned at the phase 4 stop, from mutation testing: with losses of one kind only, the sum
     * reads the same as the difference. */
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    for (int i = 0; i < 31; i++) {
        Send(&actor, PinsetterRoll(1U), &outbox); /* 30 held, and the 31st lost: no room */
    }
    Send(&actor, RollsLostReport(2U), &outbox); /* and two lost to the pinsetter's queue */
    Send(&actor, StatsQuery(9U), &outbox);
    EXPECT_EQ(3U, outbox.items[0].payload.stats.rolls_lost);
    EXPECT_EQ(30U, outbox.items[0].payload.stats.rolls_held);
}

TEST(GameActorLifecycleTest, should_answer_no_frames_and_no_total_before_a_game_whatever_memory_it_starts_in)
{
    /* Before its first NEW_GAME, the scorer has never been started: nothing in it is a game. */
    GameActor actor;
    std::memset(&actor, 0xFF, sizeof(actor));
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, StatsQuery(1U), &outbox);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.complete_frames);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.total);
}

/* ---- A league night: practice --------------------------------------------------------------- */

namespace {

Message PracticeGameRequest(RequestSeq seq, const ScorerRules &rules)
{
    Message message = NewGameRequest(seq, rules);
    message.payload.new_game.practice = true;
    return message;
}

/* A game actor, with a ten-pin game started in practice. */
GameActor MakePracticingActor()
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, PracticeGameRequest(0U, rules::kTenPin), &outbox);
    EXPECT_EQ(GAME_OK, outbox.items[0].payload.reply.status) << "setup: the practice game";
    return actor;
}

} // namespace

TEST(GameActorPracticeTest, should_not_score_a_roll_in_practice)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox); /* a frame of 7, were it scored */
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    EXPECT_EQ(0U, outbox.items[0].payload.reply.score);
    EXPECT_EQ(0U, ScoreOf(&actor));
}

TEST(GameActorPracticeTest, should_count_its_practice_balls_in_its_statistics)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, StatsQuery(3U), &outbox);
    EXPECT_EQ(2U, outbox.items[0].payload.stats.practice_balls);
}

namespace {

Message EndPracticeRequest(RequestSeq seq)
{
    Message message = RollRequest(seq, 0U);
    message.envelope.selector = MSG_END_PRACTICE;
    return message;
}

} // namespace

TEST(GameActorPracticeTest, should_score_the_balls_after_practice_ends_and_none_before)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 10U), &outbox); /* practice: a strike that earns nothing */
    Send(&actor, EndPracticeRequest(2U), &outbox);
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(3U, 3U), &outbox);
    Send(&actor, RollRequest(4U, 4U), &outbox);
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score);
}

TEST(GameActorPracticeTest, should_start_each_new_games_practice_count_at_zero)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, RollRequest(2U, 4U), &outbox);
    Send(&actor, EndPracticeRequest(3U), &outbox);
    BowlAGutterGame(&actor);
    Send(&actor, PracticeGameRequest(4U, rules::kTenPin), &outbox);
    Send(&actor, StatsQuery(5U), &outbox);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.practice_balls);
}

TEST(GameActorPracticeTest, should_count_the_pinsetters_rolls_in_practice_and_neither_score_nor_hold_them)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox);
    EXPECT_EQ(0U, outbox.count); /* from no one: no reply */
    Send(&actor, PinsetterRoll(4U), &outbox);
    Send(&actor, StatsQuery(1U), &outbox);
    EXPECT_EQ(2U, outbox.items[0].payload.stats.practice_balls);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.total);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.rolls_held);
}

TEST(GameActorPracticeTest, should_count_the_rolls_held_before_a_practice_lost_as_rolls_on_a_dead_lane)
{
    /* Rolls between games are someone rolling on a dead lane: a practice doesn't play them. */
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    BowlAGutterGame(&actor);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox); /* both held: the game is over */
    Send(&actor, PracticeGameRequest(2U, rules::kTenPin), &outbox);
    EXPECT_EQ((std::vector<int>{2}), LostEventsIn(outbox));
    Send(&actor, StatsQuery(3U), &outbox);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.rolls_held);
    EXPECT_EQ(2U, outbox.items[0].payload.stats.rolls_lost);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.total);
}

TEST(GameActorPracticeTest, should_tell_no_loss_when_a_practice_starts_with_nothing_held)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, SubscribeRequest(1U, kSubscriber), &outbox);
    BowlAGutterGame(&actor);
    Send(&actor, PracticeGameRequest(2U, rules::kTenPin), &outbox);
    EXPECT_EQ((std::vector<int>{}), LostEventsIn(outbox));
}

TEST(GameActorPracticeTest, should_refuse_to_end_a_practice_that_isnt_going_on)
{
    GameActor actor = MakeActor(rules::kTenPin); /* in play, no practice */
    TestOutbox outbox;
    Send(&actor, EndPracticeRequest(1U), &outbox);
    EXPECT_EQ(MSG_REPLY, outbox.items[0].envelope.selector);
    EXPECT_EQ(GAME_ERR_NOT_IN_PRACTICE, outbox.items[0].payload.reply.status);
}

TEST(GameActorPracticeTest, should_refuse_a_new_game_during_practice_as_a_game_in_progress)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, NewGameRequest(1U, rules::kCandlepin), &outbox);
    EXPECT_EQ(GAME_ERR_GAME_IN_PROGRESS, outbox.items[0].payload.reply.status);
}

/* ---- A league night: the pinsetter down --------------------------------------------------- */

namespace {

Message PinsetterDown(RequestSeq seq)
{
    Message message = RollRequest(seq, 0U);
    message.envelope.selector = MSG_PINSETTER_DOWN;
    return message;
}

} // namespace

TEST(GameActorPinsetterDownTest, should_refuse_and_count_the_pinsetters_rolls_while_it_is_down)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, PinsetterDown(1U), &outbox);
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox);
    Send(&actor, StatsQuery(2U), &outbox);
    EXPECT_EQ(2U, outbox.items[0].payload.stats.rolls_refused);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.total);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.rolls_held);
}

TEST(GameActorPinsetterDownTest, should_still_score_manual_rolls_and_edits_while_the_pinsetter_is_down)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, PinsetterDown(1U), &outbox);
    Send(&actor, RollRequest(2U, 3U), &outbox);
    Send(&actor, RollRequest(3U, 4U), &outbox);
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score);
    Message edit = RollRequest(4U, 0U);
    edit.envelope.selector = MSG_EDIT;
    edit.payload.edit.first_roll = 2U; /* the 4 becomes a 5: 3, 5 */
    edit.payload.edit.rolls_removed = 1U;
    edit.payload.edit.new_count = 1U;
    edit.payload.edit.new_pins[0] = 5U;
    Send(&actor, edit, &outbox);
    EXPECT_EQ(8U, outbox.items[0].payload.reply.score);
}

namespace {

Message PinsetterUp(RequestSeq seq)
{
    Message message = RollRequest(seq, 0U);
    message.envelope.selector = MSG_PINSETTER_UP;
    return message;
}

} // namespace

TEST(GameActorPinsetterDownTest, should_score_the_pinsetters_rolls_again_once_it_is_up)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, PinsetterDown(1U), &outbox);
    Send(&actor, PinsetterUp(2U), &outbox);
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    Send(&actor, PinsetterRoll(3U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox);
    EXPECT_EQ(7U, ScoreOf(&actor));
}

TEST(GameActorPinsetterDownTest, should_go_back_to_practice_when_the_pinsetter_comes_back_up)
{
    GameActor actor = MakePracticingActor();
    TestOutbox outbox;
    Send(&actor, PinsetterDown(1U), &outbox);
    Send(&actor, PinsetterRoll(3U), &outbox); /* refused: not a practice ball */
    Send(&actor, PinsetterUp(2U), &outbox);
    Send(&actor, PinsetterRoll(4U), &outbox); /* practice again */
    Send(&actor, StatsQuery(3U), &outbox);
    EXPECT_EQ(1U, outbox.items[0].payload.stats.rolls_refused);
    EXPECT_EQ(1U, outbox.items[0].payload.stats.practice_balls);
}

TEST(GameActorPinsetterDownTest, should_refuse_the_pinsetters_rolls_while_it_is_down_even_before_a_game)
{
    GameActor actor;
    GameActor_Init(&actor, kGame);
    TestOutbox outbox;
    Send(&actor, PinsetterDown(1U), &outbox);
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    Send(&actor, PinsetterRoll(3U), &outbox); /* refused, not held for the next game */
    Send(&actor, StatsQuery(2U), &outbox);
    EXPECT_EQ(1U, outbox.items[0].payload.stats.rolls_refused);
    EXPECT_EQ(0U, outbox.items[0].payload.stats.rolls_held);
}

/* ---- A league night: certified -------------------------------------------------------------- */

namespace {

Message CertifyRequest(RequestSeq seq)
{
    Message message = RollRequest(seq, 0U);
    message.envelope.selector = MSG_CERTIFY;
    return message;
}

Message EditRequest(RequestSeq seq, RollNumber first_roll, Pins pins)
{
    Message message = RollRequest(seq, 0U);
    message.envelope.selector = MSG_EDIT;
    message.payload.edit.first_roll = first_roll;
    message.payload.edit.rolls_removed = 1U;
    message.payload.edit.new_count = 1U;
    message.payload.edit.new_pins[0] = pins;
    return message;
}

} // namespace

TEST(GameActorCertifiedTest, should_refuse_every_change_to_a_certified_game)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    Send(&actor, CertifyRequest(1U), &outbox);
    EXPECT_EQ(REPLY_OK, outbox.items[0].payload.reply.status);
    for (const Message &change : {EditRequest(2U, 1U, 5U), RollRequest(3U, 5U), DiscardHeldRequest(4U)}) {
        Send(&actor, change, &outbox);
        EXPECT_EQ(GAME_ERR_CERTIFIED, outbox.items[0].payload.reply.status)
            << "selector " << change.envelope.selector;
    }
    EXPECT_EQ(0U, ScoreOf(&actor));
}

TEST(GameActorCertifiedTest, should_refuse_to_certify_a_game_that_isnt_over)
{
    GameActor actor = MakeActor(rules::kTenPin);
    TestOutbox outbox;
    Send(&actor, RollRequest(1U, 3U), &outbox);
    Send(&actor, CertifyRequest(2U), &outbox);
    EXPECT_EQ(GAME_ERR_NOT_OVER, outbox.items[0].payload.reply.status);
    Send(&actor, RollRequest(3U, 4U), &outbox); /* still in play */
    EXPECT_EQ(7U, outbox.items[0].payload.reply.score);
}

TEST(GameActorCertifiedTest, should_refuse_to_certify_a_game_with_rolls_held)
{
    GameActor actor = MakeActor(rules::kTenPin);
    BowlAGutterGame(&actor);
    TestOutbox outbox;
    Send(&actor, PinsetterRoll(3U), &outbox); /* held: the game is over */
    Send(&actor, CertifyRequest(1U), &outbox);
    EXPECT_EQ(GAME_ERR_ROLLS_HELD, outbox.items[0].payload.reply.status);
}
