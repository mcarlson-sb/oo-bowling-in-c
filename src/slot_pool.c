#include "slot_pool.h"

static bool SlotPool_Find(const SlotPool *self, const void *object, uint8_t *slot)
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

void *SlotPool_Take(SlotPool *self)
{
    unsigned char *start = (unsigned char *)self->objects;
    for (uint8_t i = 0U; i < self->size; i++) {
        if (!self->in_use[i]) {
            self->in_use[i] = true;
            return &start[(size_t)i * self->object_size];
        }
    }
    return NULL;
}

bool SlotPool_Holds(const SlotPool *self, const void *object)
{
    uint8_t slot = 0U;
    return SlotPool_Find(self, object, &slot);
}

bool SlotPool_Return(SlotPool *self, const void *object)
{
    uint8_t slot = 0U;
    if (!SlotPool_Find(self, object, &slot)) {
        return false;
    }
    const bool was_taken = self->in_use[slot];
    self->in_use[slot] = false;
    return was_taken;
}
