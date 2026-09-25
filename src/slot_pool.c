#include "slot_pool.h"

SlotPool SlotPool_Make(bool *in_use, uint8_t size)
{
    const SlotPool pool = { in_use, size };
    return pool;
}

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

void SlotPool_Release(SlotPool *self, uint8_t slot)
{
    if (slot >= self->size) {
        return;
    }
    self->in_use[slot] = false;
}
