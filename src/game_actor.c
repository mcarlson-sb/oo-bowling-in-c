#include "game_actor.h"

#include <stddef.h>

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule)
{
    Scorer_InitWithRule(&self->scorer, variant, rule);
    self->subscriber_count = 0U;
    self->held_count = 0U;
    self->held_reason = GAME_OK;
    self->lost_by_pinsetter = 0U;
    self->lost_by_actor = 0U;
}

/* ---- The outbox --------------------------------------------------------------------------- */

static GameOutput *GameOutbox_Next(GameOutbox *outbox, GameOutputKind kind, void *to)
{
    GameOutput *out = &outbox->items[outbox->count];
    outbox->count++;
    out->kind = kind;
    out->to = to;
    return out;
}

/* The reply goes first, before any event the message causes. A handler begins it, and finishes
 * it once it knows the outcome. */
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

/* A reply whose outcome is known at once. */
static void GameOutbox_Reply(GameOutbox *outbox, const GameMessage *message, GameStatus status,
                             Score score)
{
    GameReply_Finish(GameOutbox_BeginReply(outbox, message), status, score);
}

static void GameOutbox_FrameChanged(GameOutbox *outbox, void *to, const FrameEvent *frame)
{
    GameOutbox_Next(outbox, GAME_OUT_FRAME_CHANGED, to)->frame = *frame;
}

/* ---- Telling the subscribers -------------------------------------------------------------- */

/* Every subscriber hears every frame change, in the order the scorer reported them. */
static void GameActor_Publish(const GameActor *self, const FrameEvents *events,
                              GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        for (uint8_t e = 0U; e < events->count; e++) {
            GameOutbox_FrameChanged(outbox, self->subscribers[s], &events->events[e]);
        }
    }
}

/* The held roll at `index`: the ball it would be, once every roll before it is in. */
static void GameActor_PublishHeld(const GameActor *self, uint8_t index, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        GameOutput *out = GameOutbox_Next(outbox, GAME_OUT_ROLL_HELD, self->subscribers[s]);
        out->pins = self->held[index];
        out->position = (RollNumber)(Scorer_BallCount(&self->scorer) + index + 1U);
        out->held = self->held_count;
        out->status = self->held_reason;
    }
}

static void GameActor_PublishLost(const GameActor *self, GameOutbox *outbox)
{
    for (uint8_t s = 0U; s < self->subscriber_count; s++) {
        GameOutbox_Next(outbox, GAME_OUT_ROLLS_LOST, self->subscribers[s])->lost =
            (uint16_t)(self->lost_by_pinsetter + self->lost_by_actor);
    }
}

/* ---- Held rolls --------------------------------------------------------------------------- */

static void GameActor_Hold(GameActor *self, Pins pins, GameOutbox *outbox)
{
    self->held[self->held_count] = pins;
    self->held_count++;
    GameActor_PublishHeld(self, (uint8_t)(self->held_count - 1U), outbox);
}

/* No room: more than a whole game's balls are already held. */
static void GameActor_Lose(GameActor *self, GameOutbox *outbox)
{
    self->lost_by_actor++;
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

static void GameActor_DropFirstHeld(GameActor *self)
{
    self->held_count--;
    for (uint8_t i = 0U; i < self->held_count; i++) {
        self->held[i] = self->held[i + 1U];
    }
}

/* After the game changed: the held rolls, in order, until one is rejected again. */
static void GameActor_ReplayHeld(GameActor *self, GameOutbox *outbox)
{
    while (self->held_count > 0U) {
        FrameEvents events;
        const GameStatus status = Scorer_Roll(&self->scorer, self->held[0], &events);
        if (status != GAME_OK) {
            self->held_reason = status;
            GameActor_PublishHeld(self, 0U, outbox);
            return;
        }
        GameActor_DropFirstHeld(self);
        GameActor_Publish(self, &events, outbox);
    }
}

/* ---- Messages ----------------------------------------------------------------------------- */

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
    if (self->held_count > 0U) {
        GameActor_HoldOrLose(self, message->pins, outbox); /* behind the rolls already held */
        return;
    }
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, message->pins, &events);
    if (status != GAME_OK) {
        self->held_reason = status;
        GameActor_HoldOrLose(self, message->pins, outbox);
        return;
    }
    GameActor_Publish(self, &events, outbox);
}

/* The edit a message carries, pointing into the message's own copy of the new balls. */
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
        GameActor_ReplayHeld(self, outbox);
    }
    GameReply_Finish(reply, status, Scorer_Score(&self->scorer));
}

static void GameActor_DiscardHeld(GameActor *self, const GameMessage *message,
                                  GameOutbox *outbox)
{
    if (self->held_count == 0U) {
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_SUCH_ROLL, 0U);
        return;
    }
    GameOutput *reply = GameOutbox_BeginReply(outbox, message);
    GameActor_DropFirstHeld(self);
    GameActor_ReplayHeld(self, outbox);
    GameReply_Finish(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_RollsLost(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    if (message->lost != self->lost_by_pinsetter) {
        self->lost_by_pinsetter = message->lost;
        GameActor_PublishLost(self, outbox);
    }
}

/* The frames complete so far, for a subscriber that has just joined. */
static void GameActor_CatchUp(const GameActor *self, void *subscriber, GameOutbox *outbox)
{
    for (uint8_t i = 0U; i < Scorer_FrameCount(&self->scorer); i++) {
        const ScorerFrame frame = Scorer_Frame(&self->scorer, i);
        if (!frame.complete) {
            return; /* frames complete oldest first: none after this one is */
        }
        const FrameEvent event = { (FrameNumber)(i + 1U), frame.score, true };
        GameOutbox_FrameChanged(outbox, subscriber, &event);
    }
}

static void GameActor_Subscribe(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    if (self->subscriber_count == GAME_MAX_SUBSCRIBERS) {
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_ROOM, 0U);
        return;
    }
    self->subscribers[self->subscriber_count] = message->reply_to;
    self->subscriber_count++;
    GameOutbox_Reply(outbox, message, GAME_OK, 0U);
    GameActor_CatchUp(self, message->reply_to, outbox);
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
