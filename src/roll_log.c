#include "roll_log.h"

#include <assert.h>

void RollLog_Init(RollLog *self)
{
    self->count = 0U;
}

void RollLog_Append(RollLog *self, Pins pins)
{
    assert(self->count < GAME_MAX_ROLLS);
    if (self->count >= GAME_MAX_ROLLS) {
        return;
    }
    self->pins[self->count] = pins;
    self->count++;
}

uint8_t RollLog_Count(const RollLog *self)
{
    return self->count;
}

Pins RollLog_At(const RollLog *self, uint8_t index)
{
    assert(index < self->count);
    if (index >= self->count) {
        return 0U;
    }
    return self->pins[index];
}
