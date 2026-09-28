#ifndef ROLL_LOG_H
#define ROLL_LOG_H

/* Every accepted roll of a game, as the pins that fell. Plain values, so it can be copied:
 * that is how a rejected edit is undone, since the frames can't be. */

#include <stdint.h>

#include "bowling_types.h"
#include "game_limits.h"

typedef struct {
    Pins pins[GAME_MAX_ROLLS];
    uint8_t count;
} RollLog;

void RollLog_Init(RollLog *self);

/* Past GAME_MAX_ROLLS: stops a debug build, ignored in release. */
void RollLog_Append(RollLog *self, Pins pins);

uint8_t RollLog_Count(const RollLog *self);

/* Past the last roll: stops a debug build, reads as 0 in release. */
Pins RollLog_At(const RollLog *self, uint8_t index);

/* Appends `source`'s rolls from index `first` up to, not including, `end`. */
void RollLog_AppendRange(RollLog *self, const RollLog *source, uint8_t first, uint8_t end);

void RollLog_AppendPins(RollLog *self, const Pins *pins, uint8_t count);

#endif /* ROLL_LOG_H */
