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

void RollLog_AppendRange(RollLog *self, const RollLog *source, uint8_t first, uint8_t end)
{
    for (uint8_t i = first; i < end; i++) {
        RollLog_Append(self, RollLog_At(source, i));
    }
}

void RollLog_AppendPins(RollLog *self, const Pins *pins, uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++) {
        RollLog_Append(self, pins[i]);
    }
}
