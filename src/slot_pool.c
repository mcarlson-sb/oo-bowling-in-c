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

static void *SlotPool_ObjectAt(const SlotPool *self, uint8_t slot)
{
    unsigned char *start = (unsigned char *)self->objects;
    return &start[(size_t)slot * self->object_size];
}

void *SlotPool_Take(SlotPool *self)
{
    uint8_t slot = 0U;
    if (!SlotPool_Acquire(self, &slot)) {
        return NULL;
    }
    return SlotPool_ObjectAt(self, slot);
}

bool SlotPool_Holds(const SlotPool *self, const void *object)
{
    uint8_t slot = 0U;
    return SlotPool_Find(self, object, &slot);
}

bool SlotPool_Return(SlotPool *self, const void *object)
{
    uint8_t slot = 0U;
    return SlotPool_Find(self, object, &slot) && SlotPool_Release(self, slot);
}
