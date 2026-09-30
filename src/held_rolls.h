#ifndef HELD_ROLLS_H
#define HELD_ROLLS_H

/* The pinsetter's rolls a game couldn't play yet, oldest first, and why the first of them was
 * refused. A plain value, kept by the game until a correction lets them through. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_status.h"
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Pins pins[SCORER_MAX_BALLS];
    uint8_t count;
    GameStatus first_refused_for;
} HeldRolls;

void HeldRolls_Init(HeldRolls *self);

bool HeldRolls_IsEmpty(const HeldRolls *self);

bool HeldRolls_IsFull(const HeldRolls *self);

uint8_t HeldRolls_Count(const HeldRolls *self);

/* A held roll's pins, by index from the oldest, 0. */
Pins HeldRolls_PinsAt(const HeldRolls *self, uint8_t index);

/* The index of the roll held last. */
uint8_t HeldRolls_Newest(const HeldRolls *self);

void HeldRolls_Push(HeldRolls *self, Pins pins);

/* Why the game refused the oldest. */
GameStatus HeldRolls_WhyFirstRefused(const HeldRolls *self);

void HeldRolls_RefuseFirst(HeldRolls *self, GameStatus why);

void HeldRolls_DropFirst(HeldRolls *self);

#ifdef __cplusplus
}
#endif

#endif /* HELD_ROLLS_H */
