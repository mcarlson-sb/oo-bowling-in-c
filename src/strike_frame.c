#include "strike_frame.h"

#define STRIKE_PINS 10U
#define STRIKE_BONUS_ROLLS 2U

/* Safe downcast: only this file's vtable methods are called through it, and they are only
 * ever called on a StrikeFrame, whose first member is its Frame. */
static StrikeFrame *StrikeFrame_From(Frame *self)
{
    return (StrikeFrame *)self;
}

static const StrikeFrame *StrikeFrame_FromConst(const Frame *self)
{
    return (const StrikeFrame *)self;
}

static int16_t StrikeFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return (int16_t)pins;
    }

    StrikeFrame *strike = StrikeFrame_From(self);
    strike->bonus_rolls[strike->bonus_count] = pins;
    strike->bonus_count++;
    if (strike->bonus_count == STRIKE_BONUS_ROLLS) {
        self->open = false;
    }
    return (int16_t)pins;
}

static uint16_t StrikeFrame_Score(const Frame *self)
{
    if (self->open) {
        return 0U;
    }

    const StrikeFrame *strike = StrikeFrame_FromConst(self);
    uint16_t score = 0U;
    for (uint8_t i = 0U; i < self->roll_count; i++) {
        score = (uint16_t)(score + self->rolls[i]);
    }
    for (uint8_t i = 0U; i < strike->bonus_count; i++) {
        score = (uint16_t)(score + strike->bonus_rolls[i]);
    }
    return score;
}

static const FrameVtable s_vtable = {
    .roll = StrikeFrame_Roll,
    .score = StrikeFrame_Score,
};

Frame *StrikeFrame_Init(StrikeFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    self->base.rolls[0] = STRIKE_PINS;
    self->base.roll_count = 1U;
    self->bonus_count = 0U;
    return &self->base;
}
