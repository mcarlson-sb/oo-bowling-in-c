#include "tenth_frame.h"

#define ALL_PINS 10U

/* Fill balls are kept as the base class's bonus rolls, so the shared Frame_Score adds them
 * with no special case. */
static uint8_t TenthFrame_FillRollsEarned(const Frame *self)
{
    if ((self->roll_count >= 1U) && (self->rolls[0] == ALL_PINS)) {
        return 2U;
    }
    if ((self->roll_count == FRAME_MAX_ROLLS) && ((self->rolls[0] + self->rolls[1]) == ALL_PINS)) {
        return 1U;
    }
    return 0U;
}

static RollResult TenthFrame_Roll(Frame *self, uint8_t pins)
{
    if (!self->open) {
        return RollResult_Passed(pins);
    }

    const uint8_t fill_rolls_earned = TenthFrame_FillRollsEarned(self);
    if (fill_rolls_earned > 0U) {
        Frame_AddBonusRoll(self, pins);
        if (self->bonus_count == fill_rolls_earned) {
            self->open = false;
        }
        return RollResult_Consumed();
    }

    self->rolls[self->roll_count] = pins;
    self->roll_count++;
    if ((self->roll_count == FRAME_MAX_ROLLS) && (TenthFrame_FillRollsEarned(self) == 0U)) {
        self->open = false;
    }
    return RollResult_Consumed();
}

static const FrameVtable s_vtable = {
    .roll = TenthFrame_Roll,
};

Frame *TenthFrame_Init(TenthFrame *self, struct FrameContext *context)
{
    Frame_Init(&self->base, &s_vtable, context);
    return &self->base;
}
