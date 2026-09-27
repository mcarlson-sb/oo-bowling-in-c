#include "tenth_frame.h"

#include <stdbool.h>

/* Fill balls are kept as the base class's bonus rolls, so the shared Frame_Score adds them
 * with no special case. */

static RollResult TenthStrikeFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context; /* the last frame's states never change state */
    Frame_AddBonusRoll(self, pins);
    if (Frame_HasAllBonusRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Consumed();
}

/* The second fill ball rolls at what the first left standing, unless the first cleared the
 * rack. */
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
    Frame_Init(&self->base, &s_strike_vtable);
    Frame_AddRoll(&self->base, FRAME_ALL_PINS);
    return &self->base;
}

static RollResult TenthSpareFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context; /* the last frame's states never change state */
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
