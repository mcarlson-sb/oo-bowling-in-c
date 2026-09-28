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
    Pinsetter *pinsetter = SlotPool_Take(&s_pool);
    if (pinsetter == NULL) {
        Fault_Stop("pinsetter: none free; PINSETTER_POOL_SIZE is smaller than the lanes");
    }
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
    if (!SlotPool_Holds(&s_pool, pinsetter)) {
        return;
    }
    if (pinsetter->draining) {
        Fault_Stop("pinsetter: destroyed while draining");
    }
    if (!SlotPool_Return(&s_pool, pinsetter)) {
        Fault_Stop("pinsetter: destroyed twice");
    }
}

static GameStatus Pinsetter_DrainWaiting(Pinsetter *pinsetter, Game *game)
{
    /* Read once, so rolls posted during the drain wait for the next one. */
    const unsigned end = Pinsetter_PostedUpTo(pinsetter);
    for (unsigned oldest = Pinsetter_OldestAt(pinsetter); oldest != end;
         oldest = Pinsetter_Next(oldest)) {
        const GameStatus rolled = Game_Roll(game, Pinsetter_Peek(pinsetter, oldest));
        if (rolled != GAME_OK) {
            return rolled;
        }
        Pinsetter_MarkTaken(pinsetter, oldest);
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
    return Pinsetter_LostCount(pinsetter);
}

bool Pinsetter_DiscardOldest(Pinsetter *pinsetter)
{
    if (pinsetter->draining) {
        return false;
    }
    const unsigned oldest = Pinsetter_OldestAt(pinsetter);
    if (oldest == Pinsetter_PostedUpTo(pinsetter)) {
        return false;
    }
    Pinsetter_MarkTaken(pinsetter, oldest);
    return true;
}
