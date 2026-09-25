#include "regular_frame.h"

#include <stdbool.h>

#include "frame_context.h"

static bool RegularFrame_IsStrike(const Frame *self, uint8_t pins)
{
    return (self->roll_count == 0U) && (pins == 10U);
}

static bool RegularFrame_IsSpare(const Frame *self, uint8_t pins)
{
    uint16_t total = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        total = (uint16_t)(total + self->rolls[i]);
    }
    total = (uint16_t)(total + pins);
    return total == 10U;
}

static RollResult RegularFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return RollResult_Passed(pins);
    }

    FrameContext *context = self->context;
    if (RegularFrame_IsStrike(self, pins)) {
        FrameContext_SetState(context, FrameContext_NewStrikeFrame(context));
        return RollResult_Consumed();
    }
    if (RegularFrame_IsSpare(self, pins)) {
        FrameContext_SetState(context,
                              FrameContext_NewSpareFrame(context, self->rolls, self->roll_count));
        (void)FrameContext_Roll(context, pins);
        return RollResult_Consumed();
    }

    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    if (self->roll_count == FRAME_MAX_ROLLS) {
        self->open = false;
    }
    return RollResult_Consumed();
}

static const FrameVtable s_vtable = {
    .roll = RegularFrame_Roll,
};

Frame *RegularFrame_Init(RegularFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    return &self->base;
}
