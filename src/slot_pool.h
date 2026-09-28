#ifndef SLOT_POOL_H
#define SLOT_POOL_H

/* An Object Pool's bookkeeping, without the objects: the caller owns them and the flags. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Built with a static initializer, { in_use, size, objects, sizeof(object) }, over flags that
 * start false. */
typedef struct {
    bool *in_use;
    uint8_t size;
    const void *objects;
    size_t object_size;
} SlotPool;

/* False, writing nothing, if every slot is in use. */
bool SlotPool_Acquire(SlotPool *self, uint8_t *slot);

/* The slot `object` is in: false, writing nothing, for NULL, or for any address that isn't
 * the start of one of the pool's objects. Compares addresses only; never reads the object. */
bool SlotPool_Find(const SlotPool *self, const void *object, uint8_t *slot);

/* Whether the slot was in use: false for a double release, or a slot outside the pool. What
 * a double release means is the owner's to decide. */
bool SlotPool_Release(SlotPool *self, uint8_t slot);

#endif /* SLOT_POOL_H */
