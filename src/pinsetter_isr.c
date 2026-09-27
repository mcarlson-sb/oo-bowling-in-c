/* The pinsetter's interrupt side, in its own file for its own stack limit (CMakeLists.txt). */
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

static bool Pinsetter_Enqueue(Pinsetter *pinsetter, Pins pins)
{
    const unsigned post_at = atomic_load_explicit(&pinsetter->post_at, memory_order_relaxed);
    const unsigned next = Pinsetter_Next(post_at);
    if (next == atomic_load_explicit(&pinsetter->drain_at, memory_order_acquire)) {
        /* The only writer, so a load and a store: some targets can't do a lock-free
         * read-modify-write. */
        const uint16_t lost = atomic_load_explicit(&pinsetter->rolls_lost, memory_order_relaxed);
        atomic_store_explicit(&pinsetter->rolls_lost, (uint16_t)(lost + 1U),
                              memory_order_relaxed);
        return false;
    }
    pinsetter->rolls[post_at] = pins;
    atomic_store_explicit(&pinsetter->post_at, next, memory_order_release);
    return true;
}

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
