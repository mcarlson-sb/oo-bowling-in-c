#include "spare_frame.h"

/* A SpareFrame is born with both of its rolls, so every roll it sees is its one bonus
 * roll. */
static RollResult SpareFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context; /* only a RegularFrame changes state */
    Frame_AddBonusRoll(self, pins);
    Frame_Complete(self);
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = SpareFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *SpareFrame_Init(SpareFrame *self, const Frame *replaced, Pins completing_pins)
{
    return Frame_InitSpare(&self->base, &s_vtable, replaced, completing_pins);
}
