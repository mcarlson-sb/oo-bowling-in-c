#include "frame.h"

void Frame_Init(Frame *self, const FrameVtable *vtable, struct FrameContext *context)
{
    self->vtable = vtable;
    self->context = context;
    self->roll_count = 0U;
    self->bonus_count = 0U;
    self->open = true;
}

int16_t Frame_Roll(Frame *self, uint8_t pins)
{
    return self->vtable->roll(self, pins);
}

void Frame_AddBonusRoll(Frame *self, uint8_t pins)
{
    self->bonus_rolls[self->bonus_count] = pins;
    self->bonus_count++;
}

uint16_t Frame_Score(const Frame *self)
{
    if (self->open) {
        return 0U;
    }

    uint16_t score = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        score = (uint16_t)(score + self->rolls[i]);
    }
    for (uint8_t i = 0U; i < self->bonus_count; i++) {
        score = (uint16_t)(score + self->bonus_rolls[i]);
    }
    return score;
}
