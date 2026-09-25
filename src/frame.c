#include "frame.h"

#include <assert.h>

void Frame_Init(Frame *self, const FrameVtable *vtable)
{
    self->vtable = vtable;
    self->roll_count = 0U;
    self->bonus_count = 0U;
    self->complete = false;
}

RollResult Frame_Roll(Frame *self, struct FrameContext *context, uint8_t pins)
{
    /* A complete frame passes every roll on. Handled once here, so each state's roll() only
     * ever sees rolls while its frame is still incomplete. */
    if (self->complete) {
        return RollResult_Passed(pins);
    }
    return self->vtable->roll(self, context, pins);
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

/* The bounds checks stay in every build. A roll that doesn't fit is never written. Debug
 * builds also stop at the assert, so the state that sent it gets found. */
void Frame_AddRoll(Frame *self, uint8_t pins)
{
    assert(self->roll_count < FRAME_MAX_ROLLS);
    if (self->roll_count >= FRAME_MAX_ROLLS) {
        return;
    }
    self->rolls[self->roll_count] = pins;
    self->roll_count++;
}

void Frame_AddBonusRoll(Frame *self, uint8_t pins)
{
    assert(self->bonus_count < FRAME_MAX_BONUS_ROLLS);
    if (self->bonus_count >= FRAME_MAX_BONUS_ROLLS) {
        return;
    }
    self->bonus_rolls[self->bonus_count] = pins;
    self->bonus_count++;
}

void Frame_Complete(Frame *self)
{
    self->complete = true;
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
    if (!self->complete) {
        return 0U;
    }

    uint16_t score = Frame_PinsKnockedDown(self);
    for (uint8_t i = 0U; i < self->bonus_count; i++) {
        score = (uint16_t)(score + self->bonus_rolls[i]);
    }
    return score;
}

void Frame_CopyRolls(Frame *self, const Frame *from)
{
    for (uint8_t i = 0U; i < from->roll_count; i++) {
        Frame_AddRoll(self, from->rolls[i]);
    }
}
