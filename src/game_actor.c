#include "game_actor.h"

#include <assert.h>
#include <stddef.h>

static void HeldRolls_Init(HeldRolls *held)
{
    held->count = 0U;
    held->first_refused_for = GAME_OK;
}

static bool HeldRolls_IsEmpty(const HeldRolls *held)
{
    return held->count == 0U;
}

static bool HeldRolls_IsFull(const HeldRolls *held)
{
    return held->count == SCORER_MAX_BALLS;
}

static void HeldRolls_Push(HeldRolls *held, Pins pins)
{
    held->pins[held->count] = pins;
    held->count++;
}

static uint8_t HeldRolls_Newest(const HeldRolls *held)
{
    return (uint8_t)(held->count - 1U);
}

static void HeldRolls_RefuseFirst(HeldRolls *held, GameStatus why)
{
    held->first_refused_for = why;
}

static void HeldRolls_DropFirst(HeldRolls *held)
{
    held->count--;
    for (uint8_t i = 0U; i < held->count; i++) {
        held->pins[i] = held->pins[i + 1U];
    }
}

static void Subscribers_Init(Subscribers *subscribers)
{
    subscribers->count = 0U;
}

static bool Subscribers_IsFull(const Subscribers *subscribers)
{
    return subscribers->count == GAME_MAX_SUBSCRIBERS;
}

static void Subscribers_Add(Subscribers *subscribers, ActorId id)
{
    subscribers->ids[subscribers->count] = id;
    subscribers->count++;
}

static bool Subscribers_Remove(Subscribers *subscribers, ActorId id)
{
    for (uint8_t i = 0U; i < subscribers->count; i++) {
        if (subscribers->ids[i] == id) {
            subscribers->count--;
            subscribers->ids[i] = subscribers->ids[subscribers->count];
            return true;
        }
    }
    return false;
}

void GameActor_Init(GameActor *self, ActorId id, ScorerVariant variant, CountRule rule)
{
    self->id = id;
    Scorer_InitWithRule(&self->scorer, variant, rule);
    Subscribers_Init(&self->subscribers);
    HeldRolls_Init(&self->held);
    self->lost_to_full_queue = 0U;
    self->lost_to_full_held_list = 0U;
}

static Message *GameOutbox_Next(GameOutbox *outbox, Selector selector, ActorId from, ActorId to)
{
    assert(outbox->count < GAME_OUTBOX_CAPACITY);
    Message *out = &outbox->items[outbox->count];
    outbox->count++;
    out->envelope.selector = selector;
    out->envelope.from = from;
    out->envelope.to = to;
    out->envelope.seq = 0U;
    return out;
}

static Message *GameOutbox_BeginReply(GameOutbox *outbox, const Message *message)
{
    Message *reply =
        GameOutbox_Next(outbox, MSG_REPLY, message->envelope.to, message->envelope.from);
    reply->envelope.seq = message->envelope.seq;
    return reply;
}

static void GameReply_Finish(Message *reply, GameStatus status, Score score)
{
    reply->payload.reply.status = status;
    reply->payload.reply.score = score;
}

static void GameOutbox_Reply(GameOutbox *outbox, const Message *message, GameStatus status,
                             Score score)
{
    GameReply_Finish(GameOutbox_BeginReply(outbox, message), status, score);
}

static void GameOutbox_FrameChanged(GameOutbox *outbox, ActorId from, ActorId to,
                                    const FrameEvent *frame)
{
    GameOutbox_Next(outbox, MSG_FRAME_CHANGED, from, to)->payload.frame = *frame;
}

static void GameActor_Publish(const GameActor *self, const FrameEvents *events,
                              GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscribers.count; s++) {
        for (uint8_t e = 0U; e < events->count; e++) {
            GameOutbox_FrameChanged(outbox, self->id, self->subscribers.ids[s],
                                    &events->events[e]);
        }
    }
}

static RollNumber GameActor_HeldBallNumber(const GameActor *self, uint8_t index)
{
    return (RollNumber)(Scorer_BallCount(&self->scorer) + index + 1U);
}

static void GameActor_PublishHeld(const GameActor *self, uint8_t index, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscribers.count; s++) {
        Message *out = GameOutbox_Next(outbox, MSG_ROLL_HELD, self->id, self->subscribers.ids[s]);
        out->payload.roll_held.pins = self->held.pins[index];
        out->payload.roll_held.position = GameActor_HeldBallNumber(self, index);
        out->payload.roll_held.held = self->held.count;
        out->payload.roll_held.status = self->held.first_refused_for;
    }
}

static void GameActor_PublishLost(const GameActor *self, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscribers.count; s++) {
        Message *out = GameOutbox_Next(outbox, MSG_ROLLS_LOST, self->id, self->subscribers.ids[s]);
        out->payload.rolls_lost.lost =
            (uint16_t)(self->lost_to_full_queue + self->lost_to_full_held_list);
    }
}

static void GameActor_Hold(GameActor *self, Pins pins, GameOutbox *outbox)
{
    HeldRolls_Push(&self->held, pins);
    GameActor_PublishHeld(self, HeldRolls_Newest(&self->held), outbox);
}

static void GameActor_Lose(GameActor *self, GameOutbox *outbox)
{
    self->lost_to_full_held_list++;
    GameActor_PublishLost(self, outbox);
}

static void GameActor_HoldOrLose(GameActor *self, Pins pins, GameOutbox *outbox)
{
    if (HeldRolls_IsFull(&self->held)) {
        GameActor_Lose(self, outbox);
    } else {
        GameActor_Hold(self, pins, outbox);
    }
}

static GameStatus GameActor_Play(GameActor *self, Pins pins, GameOutbox *outbox)
{
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, pins, &events);
    GameActor_Publish(self, &events, outbox);
    return status;
}

static void GameActor_LetHeldRollsThrough(GameActor *self, GameOutbox *outbox)
{
    while (!HeldRolls_IsEmpty(&self->held)) {
        const GameStatus status = GameActor_Play(self, self->held.pins[0], outbox);
        if (status != GAME_OK) {
            HeldRolls_RefuseFirst(&self->held, status);
            GameActor_PublishHeld(self, 0U, outbox);
            return;
        }
        HeldRolls_DropFirst(&self->held);
    }
}

static void GameActor_Roll(GameActor *self, const Message *message, GameOutbox *outbox)
{
    Message *reply = GameOutbox_BeginReply(outbox, message);
    const GameStatus status = GameActor_Play(self, message->payload.roll.pins, outbox);
    GameReply_Finish(reply, status, Scorer_Score(&self->scorer));
}

static void GameActor_PlayOrHold(GameActor *self, Pins pins, GameOutbox *outbox)
{
    const GameStatus status = GameActor_Play(self, pins, outbox);
    if (status != GAME_OK) {
        HeldRolls_RefuseFirst(&self->held, status);
        GameActor_HoldOrLose(self, pins, outbox);
    }
}

static void GameActor_PinsetterRoll(GameActor *self, const Message *message,
                                    GameOutbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        GameActor_PlayOrHold(self, message->payload.roll.pins, outbox);
    } else {
        GameActor_HoldOrLose(self, message->payload.roll.pins, outbox);
    }
}

static RollEdit Message_Edit(const Message *message)
{
    const EditPayload *payload = &message->payload.edit;
    const RollEdit edit = { payload->first_roll, payload->rolls_removed,
                            (payload->new_count > 0U) ? payload->new_pins : NULL,
                            payload->new_count };
    return edit;
}

static void GameActor_Edit(GameActor *self, const Message *message, GameOutbox *outbox)
{
    Message *reply = GameOutbox_BeginReply(outbox, message);
    const RollEdit edit = Message_Edit(message);
    FrameEvents events;
    const GameStatus status = Scorer_Edit(&self->scorer, &edit, &events);
    GameActor_Publish(self, &events, outbox);
    if (status == GAME_OK) {
        GameActor_LetHeldRollsThrough(self, outbox);
    }
    GameReply_Finish(reply, status, Scorer_Score(&self->scorer));
}

static void GameActor_DiscardHeld(GameActor *self, const Message *message,
                                  GameOutbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_SUCH_ROLL, 0U);
        return;
    }
    Message *reply = GameOutbox_BeginReply(outbox, message);
    HeldRolls_DropFirst(&self->held);
    GameActor_LetHeldRollsThrough(self, outbox);
    GameReply_Finish(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_RollsLost(GameActor *self, const Message *message, GameOutbox *outbox)
{
    if (message->payload.rolls_lost.lost != self->lost_to_full_queue) {
        self->lost_to_full_queue = message->payload.rolls_lost.lost;
        GameActor_PublishLost(self, outbox);
    }
}

static void GameActor_QueryScore(const GameActor *self, const Message *message,
                                 GameOutbox *outbox)
{
    GameOutbox_Reply(outbox, message, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_SendCompleteFrames(const GameActor *self, ActorId subscriber,
                                        GameOutbox *outbox)
{
    for (uint8_t i = 0U; i < Scorer_FramesStarted(&self->scorer); i++) {
        const ScorerFrame frame = Scorer_Frame(&self->scorer, i);
        if (!frame.complete) {
            return;
        }
        const FrameEvent event = { (FrameNumber)(i + 1U), frame.score, true };
        GameOutbox_FrameChanged(outbox, self->id, subscriber, &event);
    }
}

static void GameActor_Subscribe(GameActor *self, const Message *message, GameOutbox *outbox)
{
    if (Subscribers_IsFull(&self->subscribers)) {
        GameOutbox_Reply(outbox, message, GAME_ERR_TOO_MANY_SUBSCRIBERS, 0U);
        return;
    }
    Subscribers_Add(&self->subscribers, message->envelope.from);
    GameOutbox_Reply(outbox, message, GAME_OK, 0U);
    GameActor_SendCompleteFrames(self, message->envelope.from, outbox);
}

static void GameActor_Unsubscribe(GameActor *self, const Message *message,
                                  GameOutbox *outbox)
{
    const bool removed = Subscribers_Remove(&self->subscribers, message->envelope.from);
    GameOutbox_Reply(outbox, message, removed ? GAME_OK : GAME_ERR_NOT_SUBSCRIBED, 0U);
}

/* What the game makes of each selector of the protocol. The ones it doesn't answer are the ones
 * not listed, which read as GAME_IGNORES. */
typedef enum {
    GAME_IGNORES = 0,
    GAME_ROLL,
    GAME_SUBSCRIBE,
    GAME_UNSUBSCRIBE,
    GAME_EDIT,
    GAME_PINSETTER_ROLL,
    GAME_QUERY_SCORE,
    GAME_DISCARD_HELD,
    GAME_ROLLS_LOST
} GameRequest;

static const GameRequest k_game_requests[MSG_SELECTOR_COUNT] = {
    [MSG_ROLL] = GAME_ROLL,
    [MSG_SUBSCRIBE] = GAME_SUBSCRIBE,
    [MSG_UNSUBSCRIBE] = GAME_UNSUBSCRIBE,
    [MSG_EDIT] = GAME_EDIT,
    [MSG_PINSETTER_ROLL] = GAME_PINSETTER_ROLL,
    [MSG_QUERY_SCORE] = GAME_QUERY_SCORE,
    [MSG_DISCARD_HELD] = GAME_DISCARD_HELD,
    [MSG_ROLLS_LOST] = GAME_ROLLS_LOST,
};

static GameRequest GameActor_RequestOf(const Message *message)
{
    const Selector selector = message->envelope.selector;
    return ((unsigned)selector < MSG_SELECTOR_COUNT) ? k_game_requests[selector] : GAME_IGNORES;
}

void GameActor_Handle(GameActor *self, const Message *message, GameOutbox *outbox)
{
    outbox->count = 0U;
    switch (GameActor_RequestOf(message)) {
    case GAME_SUBSCRIBE:
        GameActor_Subscribe(self, message, outbox);
        break;
    case GAME_UNSUBSCRIBE:
        GameActor_Unsubscribe(self, message, outbox);
        break;
    case GAME_EDIT:
        GameActor_Edit(self, message, outbox);
        break;
    case GAME_PINSETTER_ROLL:
        GameActor_PinsetterRoll(self, message, outbox);
        break;
    case GAME_DISCARD_HELD:
        GameActor_DiscardHeld(self, message, outbox);
        break;
    case GAME_ROLLS_LOST:
        GameActor_RollsLost(self, message, outbox);
        break;
    case GAME_QUERY_SCORE:
        GameActor_QueryScore(self, message, outbox);
        break;
    case GAME_ROLL:
        GameActor_Roll(self, message, outbox);
        break;
    case GAME_IGNORES:
        break;
    }
}
