#include "strike_frame.h"

static RollResult StrikeFrame_Roll(Frame *self, uint8_t pins)
{
    Frame_AddBonusRoll(self, pins);
    if (self->bonus_count == FRAME_MAX_BONUS_ROLLS) {
        Frame_Complete(self);
    }
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = StrikeFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *StrikeFrame_Init(StrikeFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    Frame_AddRoll(&self->base, FRAME_ALL_PINS);
    return &self->base;
}
