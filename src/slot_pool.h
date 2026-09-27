#ifndef SLOT_POOL_H
#define SLOT_POOL_H

/* An Object Pool's bookkeeping, without the objects: the caller owns them and the flags. */

#include <stdbool.h>
#include <stdint.h>

/* Built with a static initializer, { in_use, size }, over flags that start false. */
typedef struct {
    bool *in_use;
    uint8_t size;
} SlotPool;

/* False, writing nothing, if every slot is in use. */
bool SlotPool_Acquire(SlotPool *self, uint8_t *slot);

/* Whether the slot was in use: false for a double release, or a slot outside the pool. What
 * a double release means is the owner's to decide. */
bool SlotPool_Release(SlotPool *self, uint8_t slot);

#endif /* SLOT_POOL_H */
