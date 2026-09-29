#include "game_actor.h"

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule)
{
    Scorer_InitWithRule(&self->scorer, variant, rule);
}

static void GameOutbox_Reply(GameOutbox *outbox, const GameMessage *message, GameStatus status,
                             Score score)
{
    GameOutput *reply = &outbox->items[outbox->count];
    reply->kind = GAME_OUT_REPLY;
    reply->to = message->reply_to;
    reply->seq = message->seq;
    reply->status = status;
    reply->score = score;
    outbox->count++;
}

void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox)
{
    outbox->count = 0U;
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, message->pins, &events);
    GameOutbox_Reply(outbox, message, status, Scorer_Score(&self->scorer));
}
