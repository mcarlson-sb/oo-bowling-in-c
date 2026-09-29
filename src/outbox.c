#include "outbox.h"

#include <assert.h>

Message *Outbox_Next(Outbox *self, Selector selector, ActorId from, ActorId to)
{
    assert(self->count < OUTBOX_CAPACITY);
    Message *out = &self->items[self->count];
    self->count++;
    out->envelope.selector = selector;
    out->envelope.from = from;
    out->envelope.to = to;
    out->envelope.seq = 0U;
    return out;
}

Message *Outbox_BeginReply(Outbox *self, const Message *request)
{
    Message *reply = Outbox_Next(self, MSG_REPLY, request->envelope.to, request->envelope.from);
    reply->envelope.seq = request->envelope.seq;
    return reply;
}

void Outbox_FinishReply(Message *reply, GameStatus status, Score score)
{
    reply->payload.reply.status = status;
    reply->payload.reply.score = score;
}

void Outbox_Reply(Outbox *self, const Message *request, GameStatus status, Score score)
{
    Outbox_FinishReply(Outbox_BeginReply(self, request), status, score);
}

static bool Envelope_WantsNotUnderstood(const Envelope *envelope)
{
    return (envelope->from != ACTOR_ID_NONE) && (envelope->selector != MSG_NOT_UNDERSTOOD);
}

void Outbox_NotUnderstood(Outbox *self, ActorId from, const Message *request)
{
    if (!Envelope_WantsNotUnderstood(&request->envelope)) {
        return;
    }
    Message *reply = Outbox_Next(self, MSG_NOT_UNDERSTOOD, from, request->envelope.from);
    reply->envelope.seq = request->envelope.seq;
    reply->payload.not_understood.selector = request->envelope.selector;
}
