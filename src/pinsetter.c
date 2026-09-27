#include "pinsetter.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fault.h"
#include "game_internal.h"
#include "game_limits.h"
#include "slot_pool.h"

/* Rolls the mailbox holds before the main loop must drain it: a whole game's. A drain stops
 * at a roll the game refuses, and the rolls behind it wait until the scorer resolves it. Any
 * fewer, and the interrupt handler could lose a roll of the game while they wait. */
#define PINSETTER_CAPACITY GAME_MAX_ROLLS
_Static_assert(PINSETTER_CAPACITY >= GAME_MAX_ROLLS,
               "the mailbox must hold every roll of a game while a drain is stopped");

/* One slot more than it holds, so that a full mailbox (the next post would land on the oldest
 * waiting roll) and an empty one (nothing between the two positions) look different. */
#define PINSETTER_SLOTS (PINSETTER_CAPACITY + 1U)

/* Pinsetters available at once. There is no heap, so they come from a fixed pool. */
#define PINSETTER_POOL_SIZE 2U

/* A ring buffer with one writer on each side, so it needs no lock:
 *   - `post_at` is where the next roll goes. Only the interrupt side writes it.
 *   - `drain_at` is the oldest waiting roll. Only the main loop writes it.
 * The rolls waiting are the ones from `drain_at` up to `post_at`. Both positions stay below
 * PINSETTER_SLOTS and wrap to 0, so the capacity can be any size: no counter ever runs off
 * the end of its type. Each side publishes its own position with a release store, after
 * touching the roll, and reads the other side's with an acquire load, before touching one.
 * So a roll is always written before the main loop can see it, and read before the interrupt
 * side can reuse its place. */
/* C11 lets a compiler build an atomic out of a lock. An interrupt handler that took a lock the
 * main loop was holding would wait forever, so refuse to build where either count needs one. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "the positions must be atomic without a lock");
_Static_assert(ATOMIC_SHORT_LOCK_FREE == 2, "the lost-roll count must be atomic without a lock");

struct Pinsetter {
    Pins rolls[PINSETTER_SLOTS];
    atomic_uint post_at;
    atomic_uint drain_at;
    _Atomic uint16_t rolls_lost; /* posts ever refused. Only the interrupt side writes it */
};

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

/* The position after `position`, wrapping to the first slot. */
static unsigned Pinsetter_Next(unsigned position)
{
    return ((position + 1U) == PINSETTER_SLOTS) ? 0U : (position + 1U);
}

bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins)
{
    const unsigned post_at = atomic_load_explicit(&pinsetter->post_at, memory_order_relaxed);
    const unsigned next = Pinsetter_Next(post_at);
    if (next == atomic_load_explicit(&pinsetter->drain_at, memory_order_acquire)) {
        /* Full: never overwrite a roll the main loop hasn't seen. Count the lost roll instead.
         * This side is the only writer, so a load and a store will do: no read-modify-write,
         * which some interrupt-driven targets can't do without a lock. */
        const uint16_t lost = atomic_load_explicit(&pinsetter->rolls_lost, memory_order_relaxed);
        atomic_store_explicit(&pinsetter->rolls_lost, (uint16_t)(lost + 1U),
                              memory_order_relaxed);
        return false;
    }
    pinsetter->rolls[post_at] = pins;
    atomic_store_explicit(&pinsetter->post_at, next, memory_order_release);
    return true;
}

GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game)
{
    if (Game_IsNotifying(game)) {
        /* From inside a listener, each roll would go into the game's own mailbox, which drops
         * an impossible one: around the rule that a reported roll is never thrown away. */
        return GAME_ERR_EDIT_DURING_NOTIFICATION;
    }
    /* Only the rolls waiting now: one read of the post position, so a drain applies at most a
     * mailbox's worth, however fast the interrupt side posts. Later rolls wait for the next. */
    const unsigned post_at = atomic_load_explicit(&pinsetter->post_at, memory_order_acquire);
    unsigned drain_at = atomic_load_explicit(&pinsetter->drain_at, memory_order_relaxed);
    while (drain_at != post_at) {
        const Pins pins = pinsetter->rolls[drain_at];
        const GameStatus status = Game_Roll(game, pins); /* on the main loop's thread */
        if ((status != GAME_OK) && (status != GAME_QUEUED)) {
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
