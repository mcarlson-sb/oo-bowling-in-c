#include "frame_board.h"

void FrameBoard_Init(FrameBoard *self)
{
    for (uint8_t i = 0U; i < BOWLING_MAX_FRAMES; i++) {
        self->scores[i] = 0U;
        self->complete[i] = false;
    }
}

static bool FrameEvent_IsOfAFrameKept(const FrameEvent *frame)
{
    return (frame->frame_number >= 1U) && (frame->frame_number <= BOWLING_MAX_FRAMES);
}

void FrameBoard_Hear(FrameBoard *self, const FrameEvent *frame)
{
    if (!FrameEvent_IsOfAFrameKept(frame)) {
        return;
    }
    const uint8_t index = FrameNumber_ToIndex(frame->frame_number);
    self->scores[index] = frame->frame_score;
    self->complete[index] = frame->frame_complete;
}

uint8_t FrameBoard_CompleteCount(const FrameBoard *self)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < BOWLING_MAX_FRAMES; i++) {
        if (self->complete[i]) {
            count++;
        }
    }
    return count;
}

Score FrameBoard_Total(const FrameBoard *self)
{
    Score total = 0U;
    for (uint8_t i = 0U; i < BOWLING_MAX_FRAMES; i++) {
        if (self->complete[i]) {
            total = (Score)(total + self->scores[i]);
        }
    }
    return total;
}
