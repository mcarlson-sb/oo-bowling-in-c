#ifndef ROLL_LIST_H
#define ROLL_LIST_H

/* A frame's own rolls, or its bonus rolls. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"

#define ROLL_LIST_CAPACITY 2U

typedef struct {
    Pins pins[ROLL_LIST_CAPACITY];
    uint8_t count;
} RollList;

void RollList_Init(RollList *self);
uint8_t RollList_Count(const RollList *self);
Pins RollList_Sum(const RollList *self);
void RollList_Add(RollList *self, Pins pins);
Pins RollList_At(const RollList *self, uint8_t index);
bool RollList_IsFull(const RollList *self);

#endif /* ROLL_LIST_H */
