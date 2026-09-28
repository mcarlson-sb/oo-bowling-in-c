#ifndef SLOT_POOL_H
#define SLOT_POOL_H

/* An Object Pool over storage the caller owns: hands out and takes back the objects, keeping
 * which are taken in the caller's flags. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Built with a static initializer, { in_use, size, objects, sizeof(object) }, over flags that
 * start false. */
typedef struct {
    bool *in_use;
    uint8_t size;
    void *objects;
    size_t object_size;
} SlotPool;

/* A free object, now taken, or NULL if every one is taken. */
void *SlotPool_Take(SlotPool *self);

/* Whether `object` is one of the pool's, taken or not. */
bool SlotPool_Holds(const SlotPool *self, const void *object);

/* Whether `object` was taken: false for one already returned, or not the pool's. */
bool SlotPool_Return(SlotPool *self, const void *object);

#endif /* SLOT_POOL_H */
