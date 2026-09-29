#include "frame_board.h"

void FrameBoard_Init(FrameBoard *self)
{
    for (uint8_t i = 0U; i < SCORER_MAX_FRAMES; i++) {
        self->scores[i] = 0U;
        self->complete[i] = false;
    }
}

static bool FrameEvent_IsOfAFrameKept(const FrameEvent *frame)
{
    return (frame->frame_number >= 1U) && (frame->frame_number <= SCORER_MAX_FRAMES);
}

void FrameBoard_Hear(FrameBoard *self, const FrameEvent *frame)
{
    if (!FrameEvent_IsOfAFrameKept(frame)) {
        return;
    }
    const uint8_t index = (uint8_t)(frame->frame_number - 1U);
    self->scores[index] = frame->frame_score;
    self->complete[index] = frame->frame_complete;
}

Score FrameBoard_Total(const FrameBoard *self)
{
    Score total = 0U;
    for (uint8_t i = 0U; i < SCORER_MAX_FRAMES; i++) {
        if (self->complete[i]) {
            total = (Score)(total + self->scores[i]);
        }
    }
    return total;
}
