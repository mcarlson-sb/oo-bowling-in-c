#ifndef HELD_ROLLS_H
#define HELD_ROLLS_H

/* The pinsetter's rolls a game couldn't play yet, oldest first, why the first of them was
 * refused, and how many it had no room for. A plain value, kept by the game until a correction
 * lets them through. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_status.h"
#include "message.h"
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Pins pins[SCORER_MAX_BALLS];
    uint8_t count;
    GameStatus first_refused_for;
    uint16_t lost; /* rolls there was no room to hold, so far */
} HeldRolls;

void HeldRolls_Init(HeldRolls *self);

bool HeldRolls_IsEmpty(const HeldRolls *self);

uint8_t HeldRolls_Count(const HeldRolls *self);

/* The pins of the roll held longest: the next to be let through. */
Pins HeldRolls_Oldest(const HeldRolls *self);

/* The index of the roll held last. */
uint8_t HeldRolls_NewestIndex(const HeldRolls *self);

/* The roll at `index`, from the oldest, 0, as its subscribers hear of it: its pins, the ball it
 * would be, counting the oldest as `first_ball_number`, how many are held, and why the oldest was
 * refused. */
RollHeldPayload HeldRolls_Report(const HeldRolls *self, uint8_t index,
                                 RollNumber first_ball_number);

/* Holds the roll, newest, or counts it lost if there's no room: false then. */
bool HeldRolls_Hold(HeldRolls *self, Pins pins);

/* The rolls there was no room to hold, so far. */
uint16_t HeldRolls_Lost(const HeldRolls *self);

void HeldRolls_RefuseFirst(HeldRolls *self, GameStatus why);

void HeldRolls_DropFirst(HeldRolls *self);

#ifdef __cplusplus
}
#endif

#endif /* HELD_ROLLS_H */
