#include "scorecard.h"

#include <assert.h>

void Scorecard_Init(Scorecard *self)
{
    Scorer_Init(&self->scorer, SCORER_TEN_PIN);
}

bool Scorecard_IsOver(const Scorecard *self)
{
    return Scorer_IsOver(&self->scorer);
}

Pins Scorecard_PinsStanding(const Scorecard *self)
{
    return Scorer_PinsStanding(&self->scorer);
}

/* The facade has already checked the roll, and tells the listeners itself. */
void Scorecard_Roll(Scorecard *self, Pins pins)
{
    FrameEvents unused;
    const GameStatus status = Scorer_Roll(&self->scorer, pins, &unused);
    assert(status == GAME_OK);
    (void)status;
}

Score Scorecard_Score(const Scorecard *self)
{
    return Scorer_Score(&self->scorer);
}
