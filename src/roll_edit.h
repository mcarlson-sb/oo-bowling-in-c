#ifndef ROLL_EDIT_H
#define ROLL_EDIT_H

/* Private to the scorer core: where an edit's balls are, in the game before it and after. */

#include <stdbool.h>
#include <stdint.h>

#include "scorer.h"

bool RollEdit_IsWithinBalls(const RollEdit *edit, uint8_t ball_count);

unsigned RollEdit_BallsAfter(const RollEdit *edit, uint8_t ball_count);

/* Ball `index` of the edited game, given the game's balls before it. */
Pins RollEdit_Ball(const RollEdit *edit, const Pins *balls, uint8_t index);

#endif /* ROLL_EDIT_H */
