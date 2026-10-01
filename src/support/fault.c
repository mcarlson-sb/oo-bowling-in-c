#include "fault.h"

#include <stdio.h>
#include <stdlib.h>

/* Alone in its file, so a target's own Fault_Stop keeps this one out of the link. */
_Noreturn void Fault_Stop(const char *reason)
{
    (void)fputs(reason, stderr);
    (void)fputs("\n", stderr);
    abort();
}
