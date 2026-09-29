#include "roll_edit.h"

#include <assert.h>
#include <stddef.h>

static bool RollEdit_StartsAtABall(const RollEdit *edit, uint8_t ball_count)
{
    return (edit->first_roll != 0U) && (edit->first_roll <= ball_count);
}

static bool RollEdit_RemovesOnlyBallsThere(const RollEdit *edit, uint8_t ball_count)
{
    return ((unsigned)edit->first_roll + edit->rolls_removed) <= (ball_count + 1U);
}

bool RollEdit_IsWithinBalls(const RollEdit *edit, uint8_t ball_count)
{
    return RollEdit_StartsAtABall(edit, ball_count) &&
           RollEdit_RemovesOnlyBallsThere(edit, ball_count);
}

bool RollEdit_PromisesBallsWithoutPins(const RollEdit *edit)
{
    return (edit->new_pins == NULL) && (edit->new_count > 0U);
}

unsigned RollEdit_BallsAfter(const RollEdit *edit, uint8_t ball_count)
{
    assert(edit->rolls_removed <= ball_count);
    return ((unsigned)ball_count - edit->rolls_removed) + edit->new_count;
}

Pins RollEdit_Ball(const RollEdit *edit, const Pins *balls, uint8_t index)
{
    const uint8_t new_from = (uint8_t)(edit->first_roll - 1U);
    const uint8_t new_until = (uint8_t)(new_from + edit->new_count);
    if (index < new_from) {
        return balls[index];
    }
    if (index < new_until) {
        return edit->new_pins[index - new_from];
    }
    const uint8_t after_removed = (uint8_t)(new_from + edit->rolls_removed);
    return balls[(uint8_t)(after_removed + (index - new_until))];
}
