#include "spare_frame.h"

#include <stdbool.h>

/* Safe downcast: only this file's vtable methods are called through it, and they are only
 * ever called on a SpareFrame, whose first member is its Frame. */
static SpareFrame *SpareFrame_From(Frame *self)
{
    return (SpareFrame *)self;
}

static const SpareFrame *SpareFrame_FromConst(const Frame *self)
{
    return (const SpareFrame *)self;
}

static bool SpareFrame_IsBonusRoll(const Frame *self)
{
    return self->roll_count == FRAME_MAX_ROLLS;
}

static int16_t SpareFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return (int16_t)pins;
    }

    if (SpareFrame_IsBonusRoll(self)) {
        SpareFrame *spare = SpareFrame_From(self);
        spare->bonus_rolls[spare->bonus_count] = pins;
        spare->bonus_count++;
        self->open = false;
        return (int16_t)pins;
    }

    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    return FRAME_ROLL_CONSUMED;
}

static uint16_t SpareFrame_Score(const Frame *self)
{
    if (self->open) {
        return 0U;
    }

    const SpareFrame *spare = SpareFrame_FromConst(self);
    uint16_t score = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        score = (uint16_t)(score + self->rolls[i]);
    }
    for (uint8_t i = 0U; i < spare->bonus_count; i++) {
        score = (uint16_t)(score + spare->bonus_rolls[i]);
    }
    return score;
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
    self->bonus_count = 0U;
    return &self->base;
}
