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

static void Scorecard_AddNewFrame(Scorecard *self, Pins pins)
{
    FrameContext *new_frame = &self->frames[self->frame_count];
    if (self->frame_count == (SCORECARD_FRAMES - 1U)) {
        FrameContext_InitTenth(new_frame);
    } else {
        FrameContext_Init(new_frame);
    }
    const RollResult result = FrameContext_Roll(new_frame, pins);
    assert(result.consumed);
    (void)result;
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

bool Scorecard_AllFramesCompleteBefore(const Scorecard *self, uint8_t index)
{
    for (uint8_t i = 0U; i < index; i++) {
        if (!FrameContext_IsComplete(&self->frames[i])) {
            return false;
        }
    }
    return true;
}
