#include "game_actor.h"

#include <stddef.h>

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule)
{
    Scorer_InitWithRule(&self->scorer, variant, rule);
    self->subscriber_count = 0U;
}

static GameOutput *GameOutbox_Next(GameOutbox *outbox, GameOutputKind kind, void *to)
{
    GameOutput *out = &outbox->items[outbox->count];
    outbox->count++;
    out->kind = kind;
    out->to = to;
    return out;
}

static void GameOutbox_Reply(GameOutbox *outbox, const GameMessage *message, GameStatus status,
                             Score score)
{
    GameOutput *reply = GameOutbox_Next(outbox, GAME_OUT_REPLY, message->reply_to);
    reply->seq = message->seq;
    reply->status = status;
    reply->score = score;
}

static void GameOutbox_FrameChanged(GameOutbox *outbox, void *to, const FrameEvent *frame)
{
    GameOutbox_Next(outbox, GAME_OUT_FRAME_CHANGED, to)->frame = *frame;
}

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
    FrameEvents events;
    (void)Scorer_Roll(&self->scorer, message->pins, &events);
    GameActor_Publish(self, &events, outbox);
}

static void GameActor_Edit(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    const RollEdit edit = { message->first_roll, message->rolls_removed,
                            (message->new_count > 0U) ? message->new_pins : NULL,
                            message->new_count };
    FrameEvents events;
    const GameStatus status = Scorer_Edit(&self->scorer, &edit, &events);
    GameOutbox_Reply(outbox, message, status, Scorer_Score(&self->scorer));
    GameActor_Publish(self, &events, outbox);
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
        GameOutbox_Reply(outbox, message, GAME_ERR_NO_ROOM, Scorer_Score(&self->scorer));
        return;
    }
    self->subscribers[self->subscriber_count] = message->reply_to;
    self->subscriber_count++;
    GameOutbox_Reply(outbox, message, GAME_OK, Scorer_Score(&self->scorer));
    GameActor_CatchUp(self, message->reply_to, outbox);
}

void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    outbox->count = 0U;
    switch (message->kind) {
    case GAME_MSG_SUBSCRIBE:
        GameActor_Subscribe(self, message, outbox);
        break;
    case GAME_MSG_EDIT:
        GameActor_Edit(self, message, outbox);
        break;
    case GAME_MSG_PINSETTER_ROLL:
        GameActor_PinsetterRoll(self, message, outbox);
        break;
    case GAME_MSG_ROLL:
    default:
        GameActor_Roll(self, message, outbox);
        break;
    }
}
