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
 * it with a release store after touching the roll, and reads the other's with an acquire. The
 * helpers below are the only code that loads or stores a position. */
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

/* ---- Interrupt side ---------------------------------------------------------------------- */

/* Its own position: no other side writes it, so relaxed. */
static inline unsigned Pinsetter_PostAt(const Pinsetter *self)
{
    return atomic_load_explicit(&self->post_at, memory_order_relaxed);
}

/* Acquire: the main loop has finished reading any slot it has moved past. */
static inline bool Pinsetter_IsFull(const Pinsetter *self, unsigned next_post_at)
{
    return next_post_at == atomic_load_explicit(&self->drain_at, memory_order_acquire);
}

/* Release: the roll is written before the main loop can see the position that announces it. */
static inline void Pinsetter_MarkPosted(Pinsetter *self, unsigned next_post_at)
{
    atomic_store_explicit(&self->post_at, next_post_at, memory_order_release);
}

/* The only writer, so a load and a store: some targets can't do a lock-free read-modify-write. */
static inline void Pinsetter_CountLost(Pinsetter *self)
{
    const uint16_t lost = atomic_load_explicit(&self->rolls_lost, memory_order_relaxed);
    atomic_store_explicit(&self->rolls_lost, (uint16_t)(lost + 1U), memory_order_relaxed);
}

/* ---- Main-loop side ---------------------------------------------------------------------- */

/* Its own position: no other side writes it, so relaxed. */
static inline unsigned Pinsetter_OldestAt(const Pinsetter *self)
{
    return atomic_load_explicit(&self->drain_at, memory_order_relaxed);
}

/* Acquire: every roll before this position is written. */
static inline unsigned Pinsetter_PostedUpTo(const Pinsetter *self)
{
    return atomic_load_explicit(&self->post_at, memory_order_acquire);
}

static inline Pins Pinsetter_Peek(const Pinsetter *self, unsigned position)
{
    return self->rolls[position];
}

/* Release: the roll at `position` is read before the interrupt side can reuse its slot. */
static inline void Pinsetter_MarkTaken(Pinsetter *self, unsigned position)
{
    atomic_store_explicit(&self->drain_at, Pinsetter_Next(position), memory_order_release);
}

static inline uint16_t Pinsetter_LostCount(const Pinsetter *self)
{
    return atomic_load_explicit(&self->rolls_lost, memory_order_relaxed);
}

#endif /* PINSETTER_RING_H */
