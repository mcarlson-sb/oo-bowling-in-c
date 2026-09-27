#include "strike_frame.h"

static RollResult StrikeFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context;
    Frame_AddBonusRoll(self, pins);
    if (Frame_HasAllBonusRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = StrikeFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *StrikeFrame_Init(StrikeFrame *self)
{
    return Frame_InitStrike(&self->base, &s_vtable);
}
