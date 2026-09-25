#include "spare_frame.h"

/* A SpareFrame is born with both of its rolls, so every roll it sees is its one bonus
 * roll. */
static RollResult SpareFrame_Roll(Frame *self, uint8_t pins)
{
    Frame_AddBonusRoll(self, pins);
    Frame_Close(self);
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = SpareFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *SpareFrame_Init(SpareFrame *self, struct FrameContext *context, const Frame *replaced,
                       uint8_t completing_pins)
{
    Frame_Init(&self->base, &s_vtable, context);
    for (uint8_t i = 0U; i < replaced->roll_count; i++) {
        Frame_AddRoll(&self->base, replaced->rolls[i]);
    }
    Frame_AddRoll(&self->base, completing_pins);
    return &self->base;
}
