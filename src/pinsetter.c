/* The pinsetter's main-loop side, and its pool. The interrupt side is in pinsetter_isr.c; the
 * ring they share is in pinsetter_ring.h. */
#include "pinsetter.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fault.h"
#include "pinsetter_hooks.h"
#include "pinsetter_ring.h"
#include "slot_pool.h"

/* Pinsetters available at once. There is no heap, so they come from a fixed pool. */
#define PINSETTER_POOL_SIZE 2U

static Pinsetter s_pinsetters[PINSETTER_POOL_SIZE];
static bool s_in_use[PINSETTER_POOL_SIZE];
static SlotPool s_pool = { s_in_use, PINSETTER_POOL_SIZE };

Pinsetter *Pinsetter_Create(void)
{
    uint8_t slot = 0U;
    if (!SlotPool_Acquire(&s_pool, &slot)) {
        /* A configuration error, not a runtime condition: the system has more lanes than
         * PINSETTER_POOL_SIZE. There is no safe pinsetter to hand back, so stop, in every
         * build, at the moment the mistake is made. */
        Fault_Stop("pinsetter: none free; PINSETTER_POOL_SIZE is smaller than the lanes");
    }
    Pinsetter *pinsetter = &s_pinsetters[slot];
    atomic_store(&pinsetter->post_at, 0U);
    atomic_store(&pinsetter->drain_at, 0U);
    atomic_store(&pinsetter->rolls_lost, 0U);
#if PINSETTER_CHECK_OVERLAP
    atomic_flag_clear(&pinsetter->posting);
#endif
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

GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game)
{
    /* Only the rolls waiting now: one read of the post position, so a drain applies at most a
     * mailbox's worth, however fast the interrupt side posts. Later rolls wait for the next. */
    const unsigned post_at = atomic_load_explicit(&pinsetter->post_at, memory_order_acquire);
    unsigned drain_at = atomic_load_explicit(&pinsetter->drain_at, memory_order_relaxed);
    while (drain_at != post_at) {
        const Pins pins = pinsetter->rolls[drain_at];
        const GameStatus status = Game_Roll(game, pins); /* on the main loop's thread */
        if (status != GAME_OK) {
            return status; /* the machine reported it: keep it, and let the scorer decide */
        }
        drain_at = Pinsetter_Next(drain_at);
        atomic_store_explicit(&pinsetter->drain_at, drain_at, memory_order_release);
    }
    return GAME_OK;
}

uint16_t Pinsetter_RollsLost(const Pinsetter *pinsetter)
{
    /* Only read: never cleared, so the interrupt side stays its only writer, and any number of
     * readers can ask without changing what the others see. */
    return atomic_load_explicit(&pinsetter->rolls_lost, memory_order_relaxed);
}

bool Pinsetter_DiscardOldest(Pinsetter *pinsetter)
{
    const unsigned drain_at = atomic_load_explicit(&pinsetter->drain_at, memory_order_relaxed);
    if (drain_at == atomic_load_explicit(&pinsetter->post_at, memory_order_acquire)) {
        return false; /* nothing waiting */
    }
    atomic_store_explicit(&pinsetter->drain_at, Pinsetter_Next(drain_at), memory_order_release);
    return true;
}
