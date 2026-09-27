#include "frame_context.h"
#include "frame_transition.h"

/* Abstract Factory: one table per family of states. */
struct FrameStateFactory {
    Frame *(*new_strike)(FrameContext *self);
    Frame *(*new_spare)(FrameContext *self, const Frame *replaced, Pins completing_pins);
};

/* Frames 1 to 9: a strike or spare passes its bonus rolls on to the next frame. */
static Frame *NewPassingStrike(FrameContext *self)
{
    return StrikeFrame_Init(&self->strike);
}

static Frame *NewPassingSpare(FrameContext *self, const Frame *replaced, Pins completing_pins)
{
    return SpareFrame_Init(&self->spare, replaced, completing_pins);
}

static const struct FrameStateFactory s_regular_family = {
    .new_strike = NewPassingStrike,
    .new_spare = NewPassingSpare,
};

/* The tenth frame: a strike or spare keeps its fill balls. */
static Frame *NewTenthStrike(FrameContext *self)
{
    return TenthStrikeFrame_Init(&self->tenth_strike);
}

static Frame *NewTenthSpare(FrameContext *self, const Frame *replaced, Pins completing_pins)
{
    return TenthSpareFrame_Init(&self->tenth_spare, replaced, completing_pins);
}

static const struct FrameStateFactory s_last_frame_family = {
    .new_strike = NewTenthStrike,
    .new_spare = NewTenthSpare,
};

static void FrameContext_Start(FrameContext *self, const struct FrameStateFactory *family)
{
    self->factory = family;
    self->current_state = RegularFrame_Init(&self->regular);
}

void FrameContext_Init(FrameContext *self)
{
    FrameContext_Start(self, &s_regular_family);
}

void FrameContext_InitTenth(FrameContext *self)
{
    FrameContext_Start(self, &s_last_frame_family);
}

Frame *FrameContext_NewStrikeFrame(FrameContext *self)
{
    return self->factory->new_strike(self);
}

Frame *FrameContext_NewSpareFrame(FrameContext *self, const Frame *replaced,
                                  Pins completing_pins)
{
    return self->factory->new_spare(self, replaced, completing_pins);
}

void FrameContext_SetState(FrameContext *self, Frame *frame_state)
{
    self->current_state = frame_state;
}

RollResult FrameContext_Roll(FrameContext *self, Pins pins)
{
    return Frame_Roll(self->current_state, self, pins);
}

Score FrameContext_Score(const FrameContext *self)
{
    return Frame_Score(self->current_state);
}

bool FrameContext_IsComplete(const FrameContext *self)
{
    return Frame_IsComplete(self->current_state);
}

Pins FrameContext_PinsStanding(const FrameContext *self)
{
    return Frame_PinsStanding(self->current_state);
}
