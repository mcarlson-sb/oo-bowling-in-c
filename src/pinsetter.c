/* The pinsetter's main-loop side, and its pool. */
#include "pinsetter.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fault.h"
#include "pinsetter_hooks.h"
#include "pinsetter_ring.h"
#include "slot_pool.h"

#define PINSETTER_POOL_SIZE 2U

static Pinsetter s_pinsetters[PINSETTER_POOL_SIZE];
static bool s_in_use[PINSETTER_POOL_SIZE];
static SlotPool s_pool = { s_in_use, PINSETTER_POOL_SIZE, s_pinsetters, sizeof(Pinsetter) };

Pinsetter *Pinsetter_Create(void)
{
    uint8_t slot = 0U;
    if (!SlotPool_Acquire(&s_pool, &slot)) {
        Fault_Stop("pinsetter: none free; PINSETTER_POOL_SIZE is smaller than the lanes");
    }
    Pinsetter *pinsetter = &s_pinsetters[slot];
    atomic_store(&pinsetter->post_at, 0U);
    atomic_store(&pinsetter->drain_at, 0U);
    atomic_store(&pinsetter->rolls_lost, 0U);
    pinsetter->draining = false;
#if PINSETTER_CHECK_OVERLAP
    atomic_flag_clear(&pinsetter->posting);
#endif
    return pinsetter;
}

void Pinsetter_Destroy(Pinsetter *pinsetter)
{
    uint8_t slot = 0U;
    if (!SlotPool_Find(&s_pool, pinsetter, &slot)) {
        return;
    }
    if (pinsetter->draining) {
        Fault_Stop("pinsetter: destroyed while draining");
    }
    if (!SlotPool_Release(&s_pool, slot)) {
        Fault_Stop("pinsetter: destroyed twice");
    }
}

static GameStatus Pinsetter_DrainWaiting(Pinsetter *pinsetter, Game *game)
{
    /* Read once, so rolls posted during the drain wait for the next one. */
    const unsigned post_at = atomic_load_explicit(&pinsetter->post_at, memory_order_acquire);
    unsigned drain_at = atomic_load_explicit(&pinsetter->drain_at, memory_order_relaxed);
    while (drain_at != post_at) {
        const Pins pins = pinsetter->rolls[drain_at];
        const GameStatus status = Game_Roll(game, pins);
        if (status != GAME_OK) {
            return status;
        }
        drain_at = Pinsetter_Next(drain_at);
        atomic_store_explicit(&pinsetter->drain_at, drain_at, memory_order_release);
    }
    return GAME_OK;
}

/* The drain writes its copy of drain_at back after each roll, over any discard a listener
 * made; hence `draining`. A nested drain returns without clearing the outer one's flag. */
GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game)
{
    if (pinsetter->draining) {
        return GAME_ERR_BUSY;
    }
    pinsetter->draining = true;
    const GameStatus status = Pinsetter_DrainWaiting(pinsetter, game);
    pinsetter->draining = false;
    return status;
}

uint16_t Pinsetter_RollsLost(const Pinsetter *pinsetter)
{
    return atomic_load_explicit(&pinsetter->rolls_lost, memory_order_relaxed);
}

bool Pinsetter_DiscardOldest(Pinsetter *pinsetter)
{
    if (pinsetter->draining) {
        return false;
    }
    const unsigned drain_at = atomic_load_explicit(&pinsetter->drain_at, memory_order_relaxed);
    if (drain_at == atomic_load_explicit(&pinsetter->post_at, memory_order_acquire)) {
        return false;
    }
    atomic_store_explicit(&pinsetter->drain_at, Pinsetter_Next(drain_at), memory_order_release);
    return true;
}
