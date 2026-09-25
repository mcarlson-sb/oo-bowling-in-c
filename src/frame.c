#include "frame.h"

void Frame_Init(Frame *self, const FrameVtable *vtable, struct FrameContext *context)
{
    self->vtable = vtable;
    self->context = context;
    self->roll_count = 0U;
    self->open = true;
}

int16_t Frame_Roll(Frame *self, uint8_t pins)
{
    return self->vtable->roll(self, pins);
}

uint16_t Frame_Score(const Frame *self)
{
    return self->vtable->score(self);
}
