#include "scorecard.h"

#include <assert.h>

void Scorecard_Init(Scorecard *self)
{
    self->frame_count = 0U;
}

bool Scorecard_IsOver(const Scorecard *self)
{
    return (self->frame_count == SCORECARD_FRAMES) &&
           FrameContext_IsComplete(&self->frames[SCORECARD_FRAMES - 1U]);
}

static bool Scorecard_HasNoFrames(const Scorecard *self)
{
    return self->frame_count == 0U;
}

/* Only the latest frame can still be taking its own rolls. */
Pins Scorecard_PinsStanding(const Scorecard *self)
{
    if (Scorecard_HasNoFrames(self)) {
        return FRAME_ALL_PINS;
    }
    return FrameContext_PinsStanding(&self->frames[self->frame_count - 1U]);
}

static bool Scorecard_NextFrameIsTenth(const Scorecard *self)
{
    return self->frame_count == (SCORECARD_FRAMES - 1U);
}

/* The next frame, started as the tenth or as any other; not yet counted. */
static FrameContext *Scorecard_StartNextFrame(Scorecard *self)
{
    FrameContext *next = &self->frames[self->frame_count];
    if (Scorecard_NextFrameIsTenth(self)) {
        FrameContext_InitTenth(next);
    } else {
        FrameContext_Init(next);
    }
    return next;
}

/* A new frame always keeps its first roll. */
static void Scorecard_GiveFirstRoll(FrameContext *frame, Pins pins)
{
    const RollResult result = FrameContext_Roll(frame, pins);
    assert(result.consumed);
    (void)result;
}

static void Scorecard_AddNewFrame(Scorecard *self, Pins pins)
{
    Scorecard_GiveFirstRoll(Scorecard_StartNextFrame(self), pins);
    self->frame_count++;
}

/* Chain of Responsibility: oldest frame first, until one keeps the roll. */
static RollResult Scorecard_ApplyPinsToFrames(Scorecard *self, Pins pins)
{
    RollResult result = RollResult_Passed(pins);
    for (uint8_t i = 0U; (i < self->frame_count) && !result.consumed; i++) {
        result = FrameContext_Roll(&self->frames[i], result.pins);
    }
    return result;
}

void Scorecard_Roll(Scorecard *self, Pins pins)
{
    const RollResult result = Scorecard_ApplyPinsToFrames(self, pins);
    if (!result.consumed) {
        Scorecard_AddNewFrame(self, result.pins);
    }
}

Score Scorecard_Score(const Scorecard *self)
{
    Score score = 0U;
    for (uint8_t i = 0U; i < self->frame_count; i++) {
        score = (Score)(score + FrameContext_Score(&self->frames[i]));
    }
    return score;
}
