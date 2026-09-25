#include "frame_context.h"

void FrameContext_Init(FrameContext *self)
{
    self->current_state = RegularFrame_Init(&self->regular);
}

void FrameContext_InitTenth(FrameContext *self)
{
    self->current_state = TenthFrame_Init(&self->tenth);
}

Frame *FrameContext_NewStrikeFrame(FrameContext *self)
{
    return StrikeFrame_Init(&self->strike);
}

Frame *FrameContext_NewSpareFrame(FrameContext *self, const Frame *replaced,
                                  uint8_t completing_pins)
{
    return SpareFrame_Init(&self->spare, replaced, completing_pins);
}

void FrameContext_SetState(FrameContext *self, Frame *frame_state)
{
    self->current_state = frame_state;
}

RollResult FrameContext_Roll(FrameContext *self, uint8_t pins)
{
    return Frame_Roll(self->current_state, self, pins);
}

uint16_t FrameContext_Score(const FrameContext *self)
{
    return Frame_Score(self->current_state);
}

bool FrameContext_IsComplete(const FrameContext *self)
{
    return self->current_state->complete;
}

uint8_t FrameContext_PinsStanding(const FrameContext *self)
{
    return Frame_PinsStanding(self->current_state);
}
