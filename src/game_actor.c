#include "game_actor.h"

#include <assert.h>
#include <stddef.h>

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule)
{
    Scorer_InitWithRule(&self->scorer, variant, rule);
    self->subscriber_count = 0U;
    self->held_count = 0U;
    self->first_held_refused_for = GAME_OK;
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
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        for (uint8_t e = 0U; e < events->count; e++) {
            GameOutbox_FrameChanged(outbox, self->subscribers[s], &events->events[e]);
        }
    }
}

static RollNumber GameActor_HeldBallNumber(const GameActor *self, uint8_t index)
{
    return (RollNumber)(Scorer_BallCount(&self->scorer) + index + 1U);
}

static void GameActor_PublishHeld(const GameActor *self, uint8_t index, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        GameOutput *out = GameOutbox_Next(outbox, GAME_OUT_ROLL_HELD, self->subscribers[s]);
        out->pins = self->held[index];
        out->position = GameActor_HeldBallNumber(self, index);
        out->held = self->held_count;
        out->status = self->first_held_refused_for;
    }
}

static void GameActor_PublishLost(const GameActor *self, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        GameOutbox_Next(outbox, GAME_OUT_ROLLS_LOST, self->subscribers[s])->lost =
            (uint16_t)(self->lost_to_full_queue + self->lost_to_full_held_list);
    }
}

static void GameActor_Hold(GameActor *self, Pins pins, GameOutbox *outbox)
{
    self->held[self->held_count] = pins;
    self->held_count++;
    GameActor_PublishHeld(self, (uint8_t)(self->held_count - 1U), outbox);
}

static void GameActor_Lose(GameActor *self, GameOutbox *outbox)
{
    self->lost_to_full_held_list++;
    GameActor_PublishLost(self, outbox);
}

static void GameActor_HoldOrLose(GameActor *self, Pins pins, GameOutbox *outbox)
{
    if (self->held_count == SCORER_MAX_BALLS) {
        GameActor_Lose(self, outbox);
    } else {
        GameActor_Hold(self, pins, outbox);
    }
}

static bool GameActor_IsHoldingRolls(const GameActor *self)
{
    return self->held_count > 0U;
}

static void GameActor_DropFirstHeld(GameActor *self)
{
    self->held_count--;
    for (uint8_t i = 0U; i < self->held_count; i++) {
        self->held[i] = self->held[i + 1U];
    }
}

static void GameActor_LetHeldRollsThrough(GameActor *self, GameOutbox *outbox)
{
    while (GameActor_IsHoldingRolls(self)) {
        FrameEvents events;
        const GameStatus status = Scorer_Roll(&self->scorer, self->held[0], &events);
        if (status != GAME_OK) {
            self->first_held_refused_for = status;
            GameActor_PublishHeld(self, 0U, outbox);
            return;
        }
        GameActor_DropFirstHeld(self);
        GameActor_Publish(self, &events, outbox);
    }
}

static void GameActor_Roll(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, message->pins, &events);
    GameOutbox_Reply(outbox, message, status, Scorer_Score(&self->scorer));
    GameActor_Publish(self, &events, outbox);
}

static void GameActor_PinsetterRoll(GameActor *self, const GameMessage *message,
                                    GameOutbox *outbox)
{
    if (GameActor_IsHoldingRolls(self)) {
        GameActor_HoldOrLose(self, message->pins, outbox);
        return;
    }
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, message->pins, &events);
    if (status != GAME_OK) {
        self->first_held_refused_for = status;
        GameActor_HoldOrLose(self, message->pins, outbox);
        return;
    }
    GameActor_Publish(self, &events, outbox);
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
    if (!GameActor_IsHoldingRolls(self)) {
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_SUCH_ROLL, 0U);
        return;
    }
    GameOutput *reply = GameOutbox_BeginReply(outbox, message);
    GameActor_DropFirstHeld(self);
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
    if (self->subscriber_count == GAME_MAX_SUBSCRIBERS) {
        GameOutbox_Reply(outbox, message, GAME_ERR_TOO_MANY_SUBSCRIBERS, 0U);
        return;
    }
    self->subscribers[self->subscriber_count] = message->reply_to;
    self->subscriber_count++;
    GameOutbox_Reply(outbox, message, GAME_OK, 0U);
    GameActor_SendCompleteFrames(self, message->reply_to, outbox);
}

static void GameActor_Unsubscribe(GameActor *self, const GameMessage *message,
                                  GameOutbox *outbox)
{
    for (uint8_t i = 0U; i < self->subscriber_count; i++) {
        if (self->subscribers[i] == message->reply_to) {
            self->subscriber_count--;
            self->subscribers[i] = self->subscribers[self->subscriber_count];
            GameOutbox_Reply(outbox, message, GAME_OK, 0U);
            return;
        }
    }
    GameOutbox_Reply(outbox, message, GAME_ERR_NOT_SUBSCRIBED, 0U);
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
        GameOutbox_Reply(outbox, message, GAME_OK, Scorer_Score(&self->scorer));
        break;
    case GAME_MSG_ROLL:
        GameActor_Roll(self, message, outbox);
        break;
    }
}
