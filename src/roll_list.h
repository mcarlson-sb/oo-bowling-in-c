#ifndef ROLL_LIST_H
#define ROLL_LIST_H

/* A short list of rolls: a frame's own rolls, or its bonus rolls. A value type that owns its
 * array and its count together, so nothing else has to keep the two in step.
 *
 * Private to the library: lives in src/, not include/. */

#include <stdbool.h>
#include <stdint.h>

#define ROLL_LIST_CAPACITY 2U

typedef struct {
    uint8_t pins[ROLL_LIST_CAPACITY];
    uint8_t count;
} RollList;

void RollList_Init(RollList *self);
uint8_t RollList_Count(const RollList *self);
uint8_t RollList_Sum(const RollList *self);
void RollList_Add(RollList *self, uint8_t pins);
uint8_t RollList_At(const RollList *self, uint8_t index);
bool RollList_IsFull(const RollList *self);

#endif /* ROLL_LIST_H */
