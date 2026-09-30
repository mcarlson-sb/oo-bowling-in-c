#include "outbox.h"

#include <assert.h>

void Outbox_Init(Outbox *self, Message *storage, uint8_t capacity)
{
    self->items = storage;
    self->capacity = capacity;
    self->count = 0U;
}

Message *Outbox_Next(Outbox *self, Envelope envelope)
{
    assert(self->count < self->capacity);
    Message *out = &self->items[self->count];
    self->count++;
    out->envelope = envelope;
    return out;
}

Message *Outbox_BeginReply(Outbox *self, const Message *request)
{
    return Outbox_Next(self, Envelope_ReplyTo(&request->envelope, MSG_REPLY));
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

StatsPayload *Outbox_BeginStats(Outbox *self, const Message *request)
{
    Message *stats = Outbox_Next(self, Envelope_ReplyTo(&request->envelope, MSG_STATS));
    const StatsPayload none = { 0U, 0U, 0U, 0U, 0U };
    stats->payload.stats = none;
    return &stats->payload.stats;
}

void Outbox_NotUnderstood(Outbox *self, ActorId from, const Message *request)
{
    if (!Envelope_WantsNotUnderstood(&request->envelope)) {
        return;
    }
    Envelope answer = Envelope_ReplyTo(&request->envelope, MSG_NOT_UNDERSTOOD);
    answer.from = from;
    Outbox_Next(self, answer)->payload.not_understood.selector = request->envelope.selector;
}
