/* Coverage builds only (OO_C_COVERAGE): replaces the library's Fault_Stop at link time, and
 * saves gcov's data before aborting, so code that only a death test reaches is counted. */
#include "fault.h"

#include <stdio.h>
#include <stdlib.h>

extern void __gcov_dump(void);

_Noreturn void Fault_Stop(const char *reason)
{
    (void)fputs(reason, stderr);
    (void)fputs("\n", stderr);
    __gcov_dump();
    abort();
}
