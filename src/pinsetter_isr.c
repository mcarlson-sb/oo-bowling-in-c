/* The pinsetter's interrupt side: everything an interrupt handler runs, and nothing else. Its
 * own file so that its stack limit can be tight (see CMakeLists.txt). */
#include "pinsetter.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "fault.h"
#include "pinsetter_hooks.h"
#include "pinsetter_ring.h"

#if PINSETTER_CHECK_OVERLAP
void Pinsetter_HookPostUnderWay(Pinsetter *pinsetter)
{
    (void)atomic_flag_test_and_set(&pinsetter->posting);
}
#endif

/* Puts the roll in the ring, or counts it lost if the ring is full. */
static bool Pinsetter_Enqueue(Pinsetter *pinsetter, Pins pins)
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

/* In a debug build, a net under the one-producer rule: a post that finds another still under
 * way, from a second interrupt handler or a nested interrupt on one core, would write the same
 * slot. It catches overlapping posts, which is the failure itself, not who is posting, so it
 * needs no port. atomic_flag is the one atomic C11 promises is lock-free. The flag is set on
 * the way in and cleared on the one way out, so no early return can leave it set. */
bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins)
{
#if PINSETTER_CHECK_OVERLAP
    if (atomic_flag_test_and_set_explicit(&pinsetter->posting, memory_order_acquire)) {
        Fault_Stop("pinsetter: two posts overlap; only one interrupt handler may post");
    }
#endif
    const bool posted = Pinsetter_Enqueue(pinsetter, pins);
#if PINSETTER_CHECK_OVERLAP
    atomic_flag_clear_explicit(&pinsetter->posting, memory_order_release);
#endif
    return posted;
}
