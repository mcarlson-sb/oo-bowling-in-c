#ifndef SLOT_POOL_H
#define SLOT_POOL_H

/* Which of a fixed number of slots are in use: the bookkeeping behind an Object Pool,
 * without the objects. The caller owns both the objects and the in-use flags, and sizes
 * them at compile time; the pool only hands out and takes back slot numbers. Keeping this
 * separate means the rules for "is a game free?" don't live in the same module as the rules
 * of bowling.
 *
 * Private to the library: lives in src/, not include/. */

#include <stdbool.h>
#include <stdint.h>

/* A pool over `size` flags, all of which must start false. Built in place, with a static
 * initializer: { in_use, size }. */
typedef struct {
    bool *in_use;
    uint8_t size;
} SlotPool;

/* Takes a free slot and writes its number to *slot. Returns false, writing nothing, if every
 * slot is in use. */
bool SlotPool_Acquire(SlotPool *self, uint8_t *slot);

/* Frees a slot so it can be handed out again. Returns whether it was in use: false for a slot
 * already free, which is a double release by the caller, or for a number outside the pool,
 * which is ignored. The pool doesn't decide what a double release means; its owner does. */
bool SlotPool_Release(SlotPool *self, uint8_t slot);

#endif /* SLOT_POOL_H */
