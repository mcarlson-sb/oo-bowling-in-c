#include "subscribers.h"

void Subscribers_Init(Subscribers *self)
{
    self->count = 0U;
}

bool Subscribers_IsFull(const Subscribers *self)
{
    return self->count == GAME_MAX_SUBSCRIBERS;
}

uint8_t Subscribers_Count(const Subscribers *self)
{
    return self->count;
}

ActorId Subscribers_At(const Subscribers *self, uint8_t index)
{
    return self->ids[index];
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
