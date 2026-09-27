#include "fault.h"

#include <stdio.h>
#include <stdlib.h>

/* The host's fail-stop. Alone in this file, so a target build that defines its own Fault_Stop
 * never links this one (see fault.h). */
_Noreturn void Fault_Stop(const char *reason)
{
    (void)fputs(reason, stderr);
    (void)fputs("\n", stderr);
    abort();
}
