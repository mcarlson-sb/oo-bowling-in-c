#ifndef SCORECARD_H
#define SCORECARD_H

/* A game's frames, and how a roll moves along them. Holds FrameContexts, so it can't be
 * copied: rebuild it with Scorecard_Init and the rolls instead. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "frame_context.h"

#define SCORECARD_FRAMES 10U

typedef struct {
    FrameContext frames[SCORECARD_FRAMES];
    uint8_t frame_count;
} Scorecard;

void Scorecard_Init(Scorecard *self);

bool Scorecard_IsOver(const Scorecard *self);
Pins Scorecard_PinsStanding(const Scorecard *self);

/* On a scorecard that isn't over, with no more pins than Scorecard_PinsStanding. */
void Scorecard_Roll(Scorecard *self, Pins pins);

Score Scorecard_Score(const Scorecard *self);
bool Scorecard_AllFramesCompleteBefore(const Scorecard *self, uint8_t index);

static inline uint8_t Scorecard_FrameCount(const Scorecard *self)
{
    return self->frame_count;
}

static inline bool Scorecard_IsFrameComplete(const Scorecard *self, uint8_t index)
{
    return FrameContext_IsComplete(&self->frames[index]);
}

static inline Score Scorecard_FrameScore(const Scorecard *self, uint8_t index)
{
    return FrameContext_Score(&self->frames[index]);
}

#endif /* SCORECARD_H */
