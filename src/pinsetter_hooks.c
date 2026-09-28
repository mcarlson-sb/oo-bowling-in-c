/* The overlap check's test hook: test support, kept out of the interrupt side's file. */
#include "pinsetter_hooks.h"

#include <stdatomic.h>

#include "pinsetter_ring.h"

#if PINSETTER_CHECK_OVERLAP
void Pinsetter_HookPostUnderWay(Pinsetter *pinsetter)
{
    (void)atomic_flag_test_and_set(&pinsetter->posting);
}
#endif
