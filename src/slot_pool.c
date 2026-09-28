#include "slot_pool.h"

bool SlotPool_Acquire(SlotPool *self, uint8_t *slot)
{
    for (uint8_t i = 0U; i < self->size; i++) {
        if (!self->in_use[i]) {
            self->in_use[i] = true;
            *slot = i;
            return true;
        }
    }
    return false;
}

bool SlotPool_Find(const SlotPool *self, const void *object, uint8_t *slot)
{
    const unsigned char *start = (const unsigned char *)self->objects;
    for (uint8_t i = 0U; i < self->size; i++) {
        if ((const void *)&start[(size_t)i * self->object_size] == object) {
            *slot = i;
            return true;
        }
    }
    return false;
}

bool SlotPool_Release(SlotPool *self, uint8_t slot)
{
    if (slot >= self->size) {
        return false;
    }
    const bool was_in_use = self->in_use[slot];
    self->in_use[slot] = false;
    return was_in_use;
}
