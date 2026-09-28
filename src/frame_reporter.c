#include "frame_reporter.h"

void FrameReporter_Init(FrameReporter *self)
{
    FrameListeners_Init(&self->listeners);
    self->frames_told_complete = 0U;
}

bool FrameReporter_Add(FrameReporter *self, FrameChangedCallback callback, void *context)
{
    return FrameListeners_Add(&self->listeners, callback, context);
}

void FrameReporter_CatchUpNewest(const FrameReporter *self, const Scorecard *scorecard)
{
    for (uint8_t i = 0U; i < self->frames_told_complete; i++) {
        FrameListeners_TellNewest(&self->listeners, FrameNumber_FromIndex(i),
                                  Scorecard_FrameScore(scorecard, i), true);
    }
}
