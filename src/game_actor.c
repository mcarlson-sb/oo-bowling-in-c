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

static void Subscribers_Add(Subscribers *subscribers, void *queue)
{
    subscribers->queues[subscribers->count] = queue;
    subscribers->count++;
}

static bool Subscribers_Remove(Subscribers *subscribers, const void *queue)
{
    for (uint8_t i = 0U; i < subscribers->count; i++) {
        if (subscribers->queues[i] == queue) {
            subscribers->count--;
            subscribers->queues[i] = subscribers->queues[subscribers->count];
            return true;
        }
    }
    return false;
}

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule)
{
    Scorer_InitWithRule(&self->scorer, variant, rule);
    Subscribers_Init(&self->subscribers);
    HeldRolls_Init(&self->held);
    self->lost_to_full_queue = 0U;
    self->lost_to_full_held_list = 0U;
}

static GameOutput *GameOutbox_Next(GameOutbox *outbox, GameOutputKind kind, void *to)
{
    assert(outbox->count < GAME_OUTBOX_CAPACITY);
    GameOutput *out = &outbox->items[outbox->count];
    outbox->count++;
    out->kind = kind;
    out->to = to;
    return out;
}

static GameOutput *GameOutbox_BeginReply(GameOutbox *outbox, const GameMessage *message)
{
    GameOutput *reply = GameOutbox_Next(outbox, GAME_OUT_REPLY, message->reply_to);
    reply->seq = message->seq;
    return reply;
}

static void GameReply_Finish(GameOutput *reply, GameStatus status, Score score)
{
    reply->status = status;
    reply->score = score;
}

static void GameOutbox_Reply(GameOutbox *outbox, const GameMessage *message, GameStatus status,
                             Score score)
{
    GameReply_Finish(GameOutbox_BeginReply(outbox, message), status, score);
}

static void GameOutbox_FrameChanged(GameOutbox *outbox, void *to, const FrameEvent *frame)
{
    GameOutbox_Next(outbox, GAME_OUT_FRAME_CHANGED, to)->frame = *frame;
}

static void GameActor_Publish(const GameActor *self, const FrameEvents *events,
                              GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscribers.count; s++) {
        for (uint8_t e = 0U; e < events->count; e++) {
            GameOutbox_FrameChanged(outbox, self->subscribers.queues[s], &events->events[e]);
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
        GameOutput *out = GameOutbox_Next(outbox, GAME_OUT_ROLL_HELD, self->subscribers.queues[s]);
        out->pins = self->held.pins[index];
        out->position = GameActor_HeldBallNumber(self, index);
        out->held = self->held.count;
        out->status = self->held.first_refused_for;
    }
}

static void GameActor_PublishLost(const GameActor *self, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscribers.count; s++) {
        GameOutbox_Next(outbox, GAME_OUT_ROLLS_LOST, self->subscribers.queues[s])->lost =
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

static void GameActor_Roll(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    GameOutput *reply = GameOutbox_BeginReply(outbox, message);
    const GameStatus status = GameActor_Play(self, message->pins, outbox);
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

static void GameActor_PinsetterRoll(GameActor *self, const GameMessage *message,
                                    GameOutbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        GameActor_PlayOrHold(self, message->pins, outbox);
    } else {
        GameActor_HoldOrLose(self, message->pins, outbox);
    }
}

static RollEdit GameMessage_Edit(const GameMessage *message)
{
    const RollEdit edit = { message->first_roll, message->rolls_removed,
                            (message->new_count > 0U) ? message->new_pins : NULL,
                            message->new_count };
    return edit;
}

static void GameActor_Edit(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    GameOutput *reply = GameOutbox_BeginReply(outbox, message);
    const RollEdit edit = GameMessage_Edit(message);
    FrameEvents events;
    const GameStatus status = Scorer_Edit(&self->scorer, &edit, &events);
    GameActor_Publish(self, &events, outbox);
    if (status == GAME_OK) {
        GameActor_LetHeldRollsThrough(self, outbox);
    }
    GameReply_Finish(reply, status, Scorer_Score(&self->scorer));
}

static void GameActor_DiscardHeld(GameActor *self, const GameMessage *message,
                                  GameOutbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_SUCH_ROLL, 0U);
        return;
    }
    GameOutput *reply = GameOutbox_BeginReply(outbox, message);
    HeldRolls_DropFirst(&self->held);
    GameActor_LetHeldRollsThrough(self, outbox);
    GameReply_Finish(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_RollsLost(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    if (message->lost != self->lost_to_full_queue) {
        self->lost_to_full_queue = message->lost;
        GameActor_PublishLost(self, outbox);
    }
}

static void GameActor_QueryScore(const GameActor *self, const GameMessage *message,
                                 GameOutbox *outbox)
{
    GameOutbox_Reply(outbox, message, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_SendCompleteFrames(const GameActor *self, void *subscriber,
                                        GameOutbox *outbox)
{
    for (uint8_t i = 0U; i < Scorer_FramesStarted(&self->scorer); i++) {
        const ScorerFrame frame = Scorer_Frame(&self->scorer, i);
        if (!frame.complete) {
            return;
        }
        const FrameEvent event = { (FrameNumber)(i + 1U), frame.score, true };
        GameOutbox_FrameChanged(outbox, subscriber, &event);
    }
}

static void GameActor_Subscribe(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    if (Subscribers_IsFull(&self->subscribers)) {
        GameOutbox_Reply(outbox, message, GAME_ERR_TOO_MANY_SUBSCRIBERS, 0U);
        return;
    }
    Subscribers_Add(&self->subscribers, message->reply_to);
    GameOutbox_Reply(outbox, message, GAME_OK, 0U);
    GameActor_SendCompleteFrames(self, message->reply_to, outbox);
}

static void GameActor_Unsubscribe(GameActor *self, const GameMessage *message,
                                  GameOutbox *outbox)
{
    const bool removed = Subscribers_Remove(&self->subscribers, message->reply_to);
    GameOutbox_Reply(outbox, message, removed ? GAME_OK : GAME_ERR_NOT_SUBSCRIBED, 0U);
}

void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    outbox->count = 0U;
    switch (message->kind) {
    case GAME_MSG_SUBSCRIBE:
        GameActor_Subscribe(self, message, outbox);
        break;
    case GAME_MSG_UNSUBSCRIBE:
        GameActor_Unsubscribe(self, message, outbox);
        break;
    case GAME_MSG_EDIT:
        GameActor_Edit(self, message, outbox);
        break;
    case GAME_MSG_PINSETTER_ROLL:
        GameActor_PinsetterRoll(self, message, outbox);
        break;
    case GAME_MSG_DISCARD_HELD:
        GameActor_DiscardHeld(self, message, outbox);
        break;
    case GAME_MSG_ROLLS_LOST:
        GameActor_RollsLost(self, message, outbox);
        break;
    case GAME_MSG_QUERY_SCORE:
        GameActor_QueryScore(self, message, outbox);
        break;
    case GAME_MSG_ROLL:
        GameActor_Roll(self, message, outbox);
        break;
    }
}
