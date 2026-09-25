#include "frame_context.h"

void FrameContext_Init(FrameContext *self)
{
    self->current_state = RegularFrame_Init(&self->regular, self);
}

void FrameContext_SetState(FrameContext *self, Frame *frame_state)
{
    self->current_state = frame_state;
}

int16_t FrameContext_Roll(FrameContext *self, uint8_t pins)
{
    return Frame_Roll(self->current_state, pins);
}

uint16_t FrameContext_Score(const FrameContext *self)
{
    return Frame_Score(self->current_state);
}
