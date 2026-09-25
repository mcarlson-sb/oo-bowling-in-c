#include "frame.h"

#include <assert.h>

void Frame_Init(Frame *self, const FrameVtable *vtable, struct FrameContext *context)
{
    self->vtable = vtable;
    self->context = context;
    self->roll_count = 0U;
    self->bonus_count = 0U;
    self->open = true;
}

RollResult Frame_Roll(Frame *self, uint8_t pins)
{
    /* A finished frame passes every roll on. Handled once here, so each state's roll() only
     * ever sees rolls while its frame is still open. */
    if (!self->open) {
        return RollResult_Passed(pins);
    }
    return self->vtable->roll(self, pins);
}

uint8_t Frame_PinsStanding(const Frame *self)
{
    return self->vtable->pins_standing(self);
}

uint8_t Frame_AllPinsStanding(const Frame *self)
{
    (void)self;
    return FRAME_ALL_PINS;
}

void Frame_AddRoll(Frame *self, uint8_t pins)
{
    assert(self->roll_count < FRAME_MAX_ROLLS);
    self->rolls[self->roll_count] = pins;
    self->roll_count++;
}

void Frame_AddBonusRoll(Frame *self, uint8_t pins)
{
    self->bonus_rolls[self->bonus_count] = pins;
    self->bonus_count++;
}

void Frame_Close(Frame *self)
{
    self->open = false;
}

uint8_t Frame_PinsKnockedDown(const Frame *self)
{
    uint8_t pins = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        pins = (uint8_t)(pins + self->rolls[i]);
    }
    return pins;
}

uint16_t Frame_Score(const Frame *self)
{
    if (self->open) {
        return 0U;
    }

    uint16_t score = Frame_PinsKnockedDown(self);
    for (uint8_t i = 0U; i < self->bonus_count; i++) {
        score = (uint16_t)(score + self->bonus_rolls[i]);
    }
    return score;
}
