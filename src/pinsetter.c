#include "pinsetter.h"

#include <stddef.h>
#include <stdint.h>

#include "slot_pool.h"

/* Rolls the mailbox holds before the main loop must drain it. A power of two, so the
 * positions below can simply count up and wrap. */
#define PINSETTER_CAPACITY 8U

/* Pinsetters available at once. There is no heap, so they come from a fixed pool. */
#define PINSETTER_POOL_SIZE 2U

/* A ring buffer. `posted` counts the rolls ever posted, and `drained` the rolls ever
 * drained; the rolls waiting are the ones between them. */
struct Pinsetter {
    Pins rolls[PINSETTER_CAPACITY];
    unsigned posted;
    unsigned drained;
};

static Pinsetter s_pinsetters[PINSETTER_POOL_SIZE];
static bool s_in_use[PINSETTER_POOL_SIZE];
static SlotPool s_pool = { s_in_use, PINSETTER_POOL_SIZE };

Pinsetter *Pinsetter_Create(void)
{
    uint8_t slot = 0U;
    if (!SlotPool_Acquire(&s_pool, &slot)) {
        return NULL;
    }
    Pinsetter *pinsetter = &s_pinsetters[slot];
    pinsetter->posted = 0U;
    pinsetter->drained = 0U;
    return pinsetter;
}

void Pinsetter_Destroy(Pinsetter *pinsetter)
{
    for (uint8_t i = 0U; i < PINSETTER_POOL_SIZE; i++) {
        if (&s_pinsetters[i] == pinsetter) {
            SlotPool_Release(&s_pool, i);
        }
    }
}

bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins)
{
    pinsetter->rolls[pinsetter->posted % PINSETTER_CAPACITY] = pins;
    pinsetter->posted++;
    return true;
}

void Pinsetter_Drain(Pinsetter *pinsetter, Game *game)
{
    while (pinsetter->drained != pinsetter->posted) {
        (void)Game_Roll(game, pinsetter->rolls[pinsetter->drained % PINSETTER_CAPACITY]);
        pinsetter->drained++;
    }
}
