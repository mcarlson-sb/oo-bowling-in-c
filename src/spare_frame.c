#include "spare_frame.h"

#include <stdbool.h>

static bool SpareFrame_IsBonusRoll(const Frame *self)
{
    return self->roll_count == FRAME_MAX_ROLLS;
}

static RollResult SpareFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return RollResult_Passed(pins);
    }

    if (SpareFrame_IsBonusRoll(self)) {
        Frame_AddBonusRoll(self, pins);
        self->open = false;
        return RollResult_Passed(pins);
    }

    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    return RollResult_Consumed();
}

static const FrameVtable s_vtable = {
    .roll = SpareFrame_Roll,
};

Frame *SpareFrame_Init(SpareFrame *self, struct FrameContext *context, const uint8_t *rolls,
                       uint8_t roll_count)
{
    Frame_Init(&self->base, &s_vtable, context);
    for (uint8_t i = 0U; i < roll_count; i++) {
        self->base.rolls[i] = rolls[i];
    }
    self->base.roll_count = roll_count;
    return &self->base;
}
