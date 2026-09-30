#include "subscribers.h"

void Subscribers_Init(Subscribers *self)
{
    self->count = 0U;
}

bool Subscribers_IsFull(const Subscribers *self)
{
    return self->count == GAME_MAX_SUBSCRIBERS;
}

void Subscribers_Tell(const Subscribers *self, Outbox *outbox, const Message *event)
{
    for (uint8_t i = 0U; i < self->count; i++) {
        Envelope envelope = event->envelope;
        envelope.to = self->ids[i];
        Outbox_Next(outbox, envelope)->payload = event->payload;
    }
}

void Subscribers_Add(Subscribers *self, ActorId id)
{
    self->ids[self->count] = id;
    self->count++;
}

bool Subscribers_Remove(Subscribers *self, ActorId id)
{
    for (uint8_t i = 0U; i < self->count; i++) {
        if (self->ids[i] == id) {
            self->count--;
            self->ids[i] = self->ids[self->count];
            return true;
        }
    }
    return false;
}
