#include "frame_families.h"

#include "frame_context.h"

static Frame *NewPassingStrike(FrameContext *self)
{
    return StrikeFrame_Init(&self->strike);
}

static Frame *NewPassingSpare(FrameContext *self, const Frame *replaced, Pins completing_pins)
{
    return SpareFrame_Init(&self->spare, replaced, completing_pins);
}

const struct FrameStateFactory FrameFamily_Passing = {
    .new_strike = NewPassingStrike,
    .new_spare = NewPassingSpare,
};

static Frame *NewTenthStrike(FrameContext *self)
{
    return TenthStrikeFrame_Init(&self->tenth_strike);
}

static Frame *NewTenthSpare(FrameContext *self, const Frame *replaced, Pins completing_pins)
{
    return TenthSpareFrame_Init(&self->tenth_spare, replaced, completing_pins);
}

const struct FrameStateFactory FrameFamily_Tenth = {
    .new_strike = NewTenthStrike,
    .new_spare = NewTenthSpare,
};
