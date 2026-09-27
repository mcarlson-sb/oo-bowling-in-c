#ifndef PINSETTER_RING_H
#define PINSETTER_RING_H

/* Shared by pinsetter_isr.c and pinsetter.c only. */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "game_limits.h"
#include "pinsetter.h"
#include "pinsetter_hooks.h"

/* A whole game's, so no roll of it is lost while a drain is stopped at a rejected roll. */
#define PINSETTER_CAPACITY GAME_MAX_ROLLS
_Static_assert(PINSETTER_CAPACITY >= GAME_MAX_ROLLS,
               "the mailbox must hold every roll of a game while a drain is stopped");

/* One spare slot tells full from empty. */
#define PINSETTER_SLOTS (PINSETTER_CAPACITY + 1U)

/* An interrupt handler waiting on a lock the main loop holds would wait forever. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "the positions must be atomic without a lock");
_Static_assert(ATOMIC_SHORT_LOCK_FREE == 2, "the lost-roll count must be atomic without a lock");

/* Single-producer, single-consumer ring: each side writes only its own position, publishing
 * it with a release store after touching the roll, and reads the other's with an acquire. */
struct Pinsetter {
    Pins rolls[PINSETTER_SLOTS];
    atomic_uint post_at;             /* interrupt side */
    atomic_uint drain_at;            /* main loop */
    _Atomic uint16_t rolls_lost;     /* interrupt side */
    bool draining;                   /* main loop only, so plain */
#if PINSETTER_CHECK_OVERLAP
    atomic_flag posting;
#endif
};

static inline unsigned Pinsetter_Next(unsigned position)
{
    return ((position + 1U) == PINSETTER_SLOTS) ? 0U : (position + 1U);
}

#endif /* PINSETTER_RING_H */
