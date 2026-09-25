#include "tenth_frame.h"

#include <stdbool.h>

#define STRIKE_FILL_BALLS FRAME_MAX_BONUS_ROLLS
#define SPARE_FILL_BALLS 1U

/* Fill balls are kept as the base class's bonus rolls, so the shared Frame_Score adds them
 * with no special case. */
static uint8_t TenthFrame_FillRollsEarned(const Frame *self)
{
    const bool is_strike = (self->roll_count >= 1U) && (self->rolls[0] == FRAME_ALL_PINS);
    if (is_strike) {
        return STRIKE_FILL_BALLS;
    }
    const bool is_spare =
        (self->roll_count == FRAME_MAX_ROLLS) && (Frame_PinsKnockedDown(self) == FRAME_ALL_PINS);
    if (is_spare) {
        return SPARE_FILL_BALLS;
    }
    return 0U;
}

static RollResult TenthFrame_Roll(Frame *self, struct FrameContext *context, uint8_t pins)
{
    (void)context; /* only a RegularFrame changes state */
    const uint8_t fill_rolls_earned = TenthFrame_FillRollsEarned(self);
    if (fill_rolls_earned > 0U) {
        Frame_AddBonusRoll(self, pins);
        if (self->bonus_count == fill_rolls_earned) {
            Frame_Complete(self);
        }
        return RollResult_Consumed();
    }

    Frame_AddRoll(self, pins);
    if ((self->roll_count == FRAME_MAX_ROLLS) && (TenthFrame_FillRollsEarned(self) == 0U)) {
        Frame_Complete(self);
    }
    return RollResult_Consumed();
}

static uint8_t TenthFrame_PinsStanding(const Frame *self)
{
    const uint8_t fill_rolls_earned = TenthFrame_FillRollsEarned(self);
    if ((self->roll_count == 1U) && (fill_rolls_earned == 0U)) {
        return (uint8_t)(FRAME_ALL_PINS - Frame_PinsKnockedDown(self));
    }
    /* After a strike, the second fill ball rolls at what the first one left standing,
     * unless the first cleared the rack. */
    if ((fill_rolls_earned == STRIKE_FILL_BALLS) && (self->bonus_count == 1U) &&
        (self->bonus_rolls[0] != FRAME_ALL_PINS)) {
        return (uint8_t)(FRAME_ALL_PINS - self->bonus_rolls[0]);
    }
    return FRAME_ALL_PINS;
}

static const FrameVtable s_vtable = {
    .roll = TenthFrame_Roll,
    .pins_standing = TenthFrame_PinsStanding,
};

Frame *TenthFrame_Init(TenthFrame *self)
{
    Frame_Init(&self->base, &s_vtable);
    return &self->base;
}
