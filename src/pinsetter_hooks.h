#ifndef PINSETTER_HOOKS_H
#define PINSETTER_HOOKS_H

#include "pinsetter.h"

/* Pinsetter_Post's overlap check: on unless NDEBUG; -DPINSETTER_CHECK_OVERLAP=0 compiles it
 * out. */
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

/* Test only: leaves a post under way, as an interrupted Pinsetter_Post would. */
void Pinsetter_HookPostUnderWay(Pinsetter *pinsetter);

#ifdef __cplusplus
}
#endif
#endif

#endif /* PINSETTER_HOOKS_H */
