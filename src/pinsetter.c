#include "pinsetter.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "slot_pool.h"

/* Rolls the mailbox holds before the main loop must drain it. A power of two, so the
 * positions below can simply count up and wrap. */
#define PINSETTER_CAPACITY 8U

/* Pinsetters available at once. There is no heap, so they come from a fixed pool. */
#define PINSETTER_POOL_SIZE 2U

/* A ring buffer with one writer on each side, so it needs no lock:
 *   - `posted` counts the rolls ever posted. Only the interrupt side writes it.
 *   - `drained` counts the rolls ever drained. Only the main loop writes it.
 * The rolls waiting are the ones between them. Each side publishes its own count with a
 * release store, after touching the roll, and reads the other side's with an acquire load,
 * before touching one. So a roll is always written before the main loop can see it, and read
 * before the interrupt side can reuse its place. */
struct Pinsetter {
    Pins rolls[PINSETTER_CAPACITY];
    atomic_uint posted;
    atomic_uint drained;
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
    atomic_store(&pinsetter->posted, 0U);
    atomic_store(&pinsetter->drained, 0U);
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
    const unsigned posted = atomic_load_explicit(&pinsetter->posted, memory_order_relaxed);
    const unsigned drained = atomic_load_explicit(&pinsetter->drained, memory_order_acquire);
    if ((posted - drained) == PINSETTER_CAPACITY) {
        return false; /* full: never overwrite a roll the main loop hasn't seen */
    }
    pinsetter->rolls[posted % PINSETTER_CAPACITY] = pins;
    atomic_store_explicit(&pinsetter->posted, posted + 1U, memory_order_release);
    return true;
}

GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game)
{
    unsigned drained = atomic_load_explicit(&pinsetter->drained, memory_order_relaxed);
    while (drained != atomic_load_explicit(&pinsetter->posted, memory_order_acquire)) {
        const Pins pins = pinsetter->rolls[drained % PINSETTER_CAPACITY];
        const GameStatus status = Game_Roll(game, pins); /* on the main loop's thread */
        if ((status != GAME_OK) && (status != GAME_QUEUED)) {
            return status; /* the machine reported it: keep it, and let the scorer decide */
        }
        drained++;
        atomic_store_explicit(&pinsetter->drained, drained, memory_order_release);
    }
    return GAME_OK;
}
