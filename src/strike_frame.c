#include "strike_frame.h"

#define STRIKE_PINS 10U

static RollResult StrikeFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return RollResult_Passed(pins);
    }

    Frame_AddBonusRoll(self, pins);
    if (self->bonus_count == FRAME_MAX_BONUS_ROLLS) {
        self->open = false;
    }
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = StrikeFrame_Roll,
};

Frame *StrikeFrame_Init(StrikeFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    self->base.rolls[0] = STRIKE_PINS;
    self->base.roll_count = 1U;
    return &self->base;
}
