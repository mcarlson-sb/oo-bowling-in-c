#include "frame_context.h"

void FrameContext_Init(FrameContext *self)
{
    self->current_state = RegularFrame_Init(&self->regular, self);
}

void FrameContext_InitTenth(FrameContext *self)
{
    self->current_state = TenthFrame_Init(&self->tenth, self);
}

Frame *FrameContext_NewStrikeFrame(FrameContext *self)
{
    return StrikeFrame_Init(&self->strike, self);
}

Frame *FrameContext_NewSpareFrame(FrameContext *self, const uint8_t *rolls, uint8_t roll_count)
{
    return SpareFrame_Init(&self->spare, self, rolls, roll_count);
}

void FrameContext_SetState(FrameContext *self, Frame *frame_state)
{
    self->current_state = frame_state;
}

RollResult FrameContext_Roll(FrameContext *self, uint8_t pins)
{
    return Frame_Roll(self->current_state, pins);
}

uint16_t FrameContext_Score(const FrameContext *self)
{
    return Frame_Score(self->current_state);
}

bool FrameContext_IsOpen(const FrameContext *self)
{
    return self->current_state->open;
}
