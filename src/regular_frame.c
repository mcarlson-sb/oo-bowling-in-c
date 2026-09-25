#include "regular_frame.h"

static int16_t RegularFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return (int16_t)pins;
    }

    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    if (self->roll_count == FRAME_MAX_ROLLS) {
        self->open = false;
    }
    return FRAME_ROLL_CONSUMED;
}

static uint16_t RegularFrame_Score(const Frame *self)
{
    if (self->open) {
        return 0U;
    }

    uint16_t score = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        score = (uint16_t)(score + self->rolls[i]);
    }
    return score;
}

static const FrameVtable s_vtable = {
    .roll = RegularFrame_Roll,
    .score = RegularFrame_Score,
};

Frame *RegularFrame_Init(RegularFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    return &self->base;
}
