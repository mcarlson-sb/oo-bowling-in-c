#ifndef FRAME_REPORTER_H
#define FRAME_REPORTER_H

/* Tells a game's listeners what changed on its scorecard. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "frame_listeners.h"
#include "game.h"
#include "scorecard.h"

typedef struct {
    FrameListeners listeners;
    uint8_t frames_told_complete;
} FrameReporter;

void FrameReporter_Init(FrameReporter *self);

/* False, adding nothing, if the list is full or `callback` is NULL. */
bool FrameReporter_Add(FrameReporter *self, FrameChangedCallback callback, void *context);

/* Only the newest listener: the others would take a repeat for an update. */
void FrameReporter_CatchUpNewest(const FrameReporter *self, const Scorecard *scorecard);

static inline uint8_t FrameReporter_FramesToldComplete(const FrameReporter *self)
{
    return self->frames_told_complete;
}

static inline FrameNumber FrameNumber_FromIndex(uint8_t index)
{
    return (FrameNumber)(index + 1U);
}

/* Up to the scorecard's last frame, or past it to the last one they were told was complete,
 * when an edit took frames away. */
static inline uint8_t FrameReporter_FramesToReport(const Scorecard *scorecard,
                                                   uint8_t were_told_complete)
{
    const uint8_t frame_count = Scorecard_FrameCount(scorecard);
    return (were_told_complete > frame_count) ? were_told_complete : frame_count;
}

static inline bool FrameReporter_IsComplete(const Scorecard *scorecard, uint8_t index)
{
    return (index < Scorecard_FrameCount(scorecard)) && Scorecard_IsFrameComplete(scorecard, index);
}

static inline void FrameReporter_TellComplete(FrameReporter *self, const Scorecard *scorecard,
                                              uint8_t index)
{
    const FrameNumber frame_number = FrameNumber_FromIndex(index);
    FrameListeners_Tell(&self->listeners, frame_number, Scorecard_FrameScore(scorecard, index),
                        true);
    self->frames_told_complete = frame_number;
}

static inline void FrameReporter_TellReopened(const FrameReporter *self, uint8_t index)
{
    FrameListeners_Tell(&self->listeners, FrameNumber_FromIndex(index), 0U, false);
}

/* Tells the listeners about the frames from index `first` on. Inline, like its helpers and the
 * two below, because every callback's stack sits on top of it. */
static inline void FrameReporter_Tell(FrameReporter *self, const Scorecard *scorecard,
                                      uint8_t first, uint8_t were_told_complete)
{
    self->frames_told_complete = first;
    const uint8_t frames = FrameReporter_FramesToReport(scorecard, were_told_complete);
    for (uint8_t i = first; i < frames; i++) {
        if (FrameReporter_IsComplete(scorecard, i)) {
            FrameReporter_TellComplete(self, scorecard, i);
        } else if (i < were_told_complete) {
            FrameReporter_TellReopened(self, i);
        }
    }
    assert(Scorecard_AllFramesCompleteBefore(scorecard, self->frames_told_complete));
}

/* A roll can't change or reopen a frame already reported. */
static inline void FrameReporter_AfterRoll(FrameReporter *self, const Scorecard *scorecard)
{
    FrameReporter_Tell(self, scorecard, self->frames_told_complete, self->frames_told_complete);
}

static inline void FrameReporter_AfterEdit(FrameReporter *self, const Scorecard *scorecard,
                                           uint8_t were_told_complete)
{
    FrameReporter_Tell(self, scorecard, 0U, were_told_complete);
}

#endif /* FRAME_REPORTER_H */
