#ifndef PINSETTER_RING_H
#define PINSETTER_RING_H

/* The pinsetter's ring, shared by its two sides and nothing else: the interrupt side in
 * pinsetter_isr.c, and the main-loop side in pinsetter.c. They are separate files so that the
 * interrupt side can have a stack limit of its own (see CMakeLists.txt).
 *
 * Private to those two files: lives in src/, and nothing else includes it. */

#include <stdatomic.h>
#include <stdint.h>

#include "bowling_types.h"
#include "game_limits.h"
#include "pinsetter.h"
#include "pinsetter_hooks.h"

/* Rolls the mailbox holds before the main loop must drain it: a whole game's. A drain stops
 * at a roll the game refuses, and the rolls behind it wait until the scorer resolves it. Any
 * fewer, and the interrupt handler could lose a roll of the game while they wait. */
#define PINSETTER_CAPACITY GAME_MAX_ROLLS
_Static_assert(PINSETTER_CAPACITY >= GAME_MAX_ROLLS,
               "the mailbox must hold every roll of a game while a drain is stopped");

/* One slot more than it holds, so that a full mailbox (the next post would land on the oldest
 * waiting roll) and an empty one (nothing between the two positions) look different. */
#define PINSETTER_SLOTS (PINSETTER_CAPACITY + 1U)

/* C11 lets a compiler build an atomic out of a lock. An interrupt handler that took a lock the
 * main loop was holding would wait forever, so refuse to build where either count needs one. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "the positions must be atomic without a lock");
_Static_assert(ATOMIC_SHORT_LOCK_FREE == 2, "the lost-roll count must be atomic without a lock");

/* A ring buffer with one writer on each side, so it needs no lock:
 *   - `post_at` is where the next roll goes. Only the interrupt side writes it.
 *   - `drain_at` is the oldest waiting roll. Only the main loop writes it.
 * The rolls waiting are the ones from `drain_at` up to `post_at`. Both positions stay below
 * PINSETTER_SLOTS and wrap to 0, so the capacity can be any size: no counter ever runs off
 * the end of its type. Each side publishes its own position with a release store, after
 * touching the roll, and reads the other side's with an acquire load, before touching one.
 * So a roll is always written before the main loop can see it, and read before the interrupt
 * side can reuse its place. */
struct Pinsetter {
    Pins rolls[PINSETTER_SLOTS];
    atomic_uint post_at;
    atomic_uint drain_at;
    _Atomic uint16_t rolls_lost; /* posts ever refused. Only the interrupt side writes it */
#if PINSETTER_CHECK_OVERLAP
    atomic_flag posting; /* set while a post is under way: see Pinsetter_Post */
#endif
};

/* The position after `position`, wrapping to the first slot. Inline, so the interrupt side
 * pays no call for it. */
static inline unsigned Pinsetter_Next(unsigned position)
{
    return ((position + 1U) == PINSETTER_SLOTS) ? 0U : (position + 1U);
}

#endif /* PINSETTER_RING_H */
