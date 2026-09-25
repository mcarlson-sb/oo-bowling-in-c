#include "roll_list.h"

#include <assert.h>

void RollList_Init(RollList *self)
{
    self->count = 0U;
}

uint8_t RollList_Count(const RollList *self)
{
    return self->count;
}

Pins RollList_Sum(const RollList *self)
{
    Pins sum = 0U;
    for (uint8_t i = 0U; i < self->count; i++) {
        sum = (Pins)(sum + self->pins[i]);
    }
    return sum;
}

/* The bounds check stays in every build: a roll that doesn't fit is never written. Debug
 * builds also stop at the assert, so whatever sent it gets found. */
void RollList_Add(RollList *self, Pins pins)
{
    assert(!RollList_IsFull(self));
    if (RollList_IsFull(self)) {
        return;
    }
    self->pins[self->count] = pins;
    self->count++;
}

/* Reading a roll not yet made is a bug in the caller: it stops debug builds and reads as 0
 * in release, never as stale memory. */
Pins RollList_At(const RollList *self, uint8_t index)
{
    assert(index < self->count);
    if (index >= self->count) {
        return 0U;
    }
    return self->pins[index];
}

bool RollList_IsFull(const RollList *self)
{
    return self->count == ROLL_LIST_CAPACITY;
}
