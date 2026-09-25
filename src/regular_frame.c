#include "regular_frame.h"

#include <stdbool.h>

#include "frame_context.h"

static bool RegularFrame_IsStrike(const Frame *self, Pins pins)
{
    return Frame_IsFirstRoll(self) && (pins == FRAME_ALL_PINS);
}

static bool RegularFrame_IsSpare(const Frame *self, Pins pins)
{
    return (Frame_PinsKnockedDown(self) + pins) == FRAME_ALL_PINS;
}

static RollResult RegularFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    if (RegularFrame_IsStrike(self, pins)) {
        FrameContext_SetState(context, FrameContext_NewStrikeFrame(context));
        return RollResult_Consumed();
    }
    if (RegularFrame_IsSpare(self, pins)) {
        FrameContext_SetState(context, FrameContext_NewSpareFrame(context, self, pins));
        return RollResult_Consumed();
    }

    Frame_AddRoll(self, pins);
    if (Frame_HasAllRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Consumed();
}

/* While the frame is taking its own rolls, the next one is at whatever its earlier rolls
 * left standing: all ten before the first roll, fewer after it. Once complete, the next
 * roll starts a new frame on a full rack. */
static Pins RegularFrame_PinsStanding(const Frame *self)
{
    if (self->complete) {
        return FRAME_ALL_PINS;
    }
    return (Pins)(FRAME_ALL_PINS - Frame_PinsKnockedDown(self));
}

static const FrameVtable s_vtable = {
    .roll = RegularFrame_Roll,
    .pins_standing = RegularFrame_PinsStanding,
};

Frame *RegularFrame_Init(RegularFrame *self)
{
    Frame_Init(&self->base, &s_vtable);
    return &self->base;
}
