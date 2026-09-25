#include "spare_frame.h"

static int16_t SpareFrame_Roll(Frame *self, uint8_t pins)
{
    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    return FRAME_ROLL_CONSUMED;
}

static uint16_t SpareFrame_Score(const Frame *self)
{
    (void)self;
    return 0U;
}

static const FrameVtable s_vtable = {
    .roll = SpareFrame_Roll,
    .score = SpareFrame_Score,
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
