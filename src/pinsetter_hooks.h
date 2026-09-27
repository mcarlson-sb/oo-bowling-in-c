#ifndef PINSETTER_HOOKS_H
#define PINSETTER_HOOKS_H

/* The pinsetter's debug-only overlap check, and the one hook its white-box test needs.
 * Private: in src/, so nothing outside the library and its tests can include it. */

#include "pinsetter.h"

/* Whether Pinsetter_Post checks that no other post is under way on the same pinsetter. On
 * unless NDEBUG; a target build may turn it off (-DPINSETTER_CHECK_OVERLAP=0), and then the
 * check and its flag compile out entirely. */
#ifndef PINSETTER_CHECK_OVERLAP
#ifdef NDEBUG
#define PINSETTER_CHECK_OVERLAP 0
#else
#define PINSETTER_CHECK_OVERLAP 1
#endif
#endif

#if PINSETTER_CHECK_OVERLAP
#ifdef __cplusplus
extern "C" {
#endif

/* For the test only: marks a post as under way on `pinsetter`, as an interrupt handler that
 * was interrupted in the middle of Pinsetter_Post would leave it. */
void Pinsetter_HookPostUnderWay(Pinsetter *pinsetter);

#ifdef __cplusplus
}
#endif
#endif

#endif /* PINSETTER_HOOKS_H */
