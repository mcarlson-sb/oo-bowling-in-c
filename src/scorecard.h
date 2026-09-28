#ifndef SCORECARD_H
#define SCORECARD_H

/* A game's frames, for the Game facade: now an adapter over the scorer core, which holds the
 * balls as plain values and works out the frames from them. The facade counts each roll with
 * its PinCountRule before giving it here, so the core counts the pins it is given. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "scorer.h"

typedef struct {
    Scorer scorer;
} Scorecard;

void Scorecard_Init(Scorecard *self);

bool Scorecard_IsOver(const Scorecard *self);
Pins Scorecard_PinsStanding(const Scorecard *self);

/* On a scorecard that isn't over, with no more pins than Scorecard_PinsStanding. */
void Scorecard_Roll(Scorecard *self, Pins pins);

Score Scorecard_Score(const Scorecard *self);

static inline uint8_t Scorecard_FrameCount(const Scorecard *self)
{
    return Scorer_FrameCount(&self->scorer);
}

static inline bool Scorecard_IsFrameComplete(const Scorecard *self, uint8_t index)
{
    return Scorer_Frame(&self->scorer, index).complete;
}

static inline Score Scorecard_FrameScore(const Scorecard *self, uint8_t index)
{
    return Scorer_Frame(&self->scorer, index).score;
}

/* Inline, so a release build, where only asserts use it, doesn't carry it. */
static inline bool Scorecard_AllFramesCompleteBefore(const Scorecard *self, uint8_t index)
{
    for (uint8_t i = 0U; i < index; i++) {
        if (!Scorecard_IsFrameComplete(self, i)) {
            return false;
        }
    }
    return true;
}

#endif /* SCORECARD_H */
