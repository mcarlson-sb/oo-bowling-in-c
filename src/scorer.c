#include "scorer.h"

void Scorer_Init(Scorer *self, ScorerVariant variant)
{
    self->variant = variant;
    self->ball_count = 0U;
}

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events)
{
    events->count = 0U;
    self->balls[self->ball_count] = pins;
    self->ball_count++;
    return GAME_OK;
}

Score Scorer_Score(const Scorer *self)
{
    Score score = 0U;
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        score = (Score)(score + self->balls[i]);
    }
    return score;
}
