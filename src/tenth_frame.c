#include "tenth_frame.h"

#include <stdbool.h>

/* Fill balls are bonus rolls, so Frame_Score needs no special case. */

static RollResult TenthStrikeFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context;
    Frame_AddBonusRoll(self, pins);
    if (Frame_HasAllBonusRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Consumed();
}

static Pins TenthStrikeFrame_PinsStanding(const Frame *self)
{
    if (!Frame_IsSecondBonusRoll(self)) {
        return FRAME_ALL_PINS;
    }
    const Pins first_fill = Frame_FirstBonusRoll(self);
    const bool first_fill_was_a_strike = (first_fill == FRAME_ALL_PINS);
    if (first_fill_was_a_strike) {
        return FRAME_ALL_PINS;
    }
    return (Pins)(FRAME_ALL_PINS - first_fill);
}

static const FrameVtable s_strike_vtable = {
    .roll = TenthStrikeFrame_Roll,
    .pins_standing = TenthStrikeFrame_PinsStanding,
};

Frame *TenthStrikeFrame_Init(TenthStrikeFrame *self)
{
    return Frame_InitStrike(&self->base, &s_strike_vtable);
}

static RollResult TenthSpareFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context;
    Frame_AddBonusRoll(self, pins);
    Frame_Complete(self);
    return RollResult_Consumed();
}

static const FrameVtable s_spare_vtable = {
    .roll = TenthSpareFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *TenthSpareFrame_Init(TenthSpareFrame *self, const Frame *replaced, Pins completing_pins)
{
    return Frame_InitSpare(&self->base, &s_spare_vtable, replaced, completing_pins);
}
