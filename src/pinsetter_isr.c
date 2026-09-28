/* The pinsetter's interrupt side, in its own file for its own stack limit (CMakeLists.txt). */
#include "pinsetter.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "fault.h"
#include "pinsetter_hooks.h"
#include "pinsetter_ring.h"

static bool Pinsetter_Enqueue(Pinsetter *pinsetter, Pins pins)
{
    const unsigned post_at = Pinsetter_PostAt(pinsetter);
    const unsigned next = Pinsetter_Next(post_at);
    if (Pinsetter_IsFull(pinsetter, next)) {
        Pinsetter_CountLost(pinsetter);
        return false;
    }
    pinsetter->rolls[post_at] = pins;
    Pinsetter_MarkPosted(pinsetter, next);
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
