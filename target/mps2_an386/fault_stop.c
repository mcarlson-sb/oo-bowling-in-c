#include "fault.h"

#include "semihosting.h"

/* The QEMU image's fail-stop: says why, and ends QEMU with a failing status, so a test or a smoke
 * run that hits a fault fails. Linked as an object, so the host's in src/support stays out. */
_Noreturn void Fault_Stop(const char *reason)
{
    Semihosting_Write("FAULT: ");
    Semihosting_Write(reason);
    Semihosting_Write("\n");
    Semihosting_Exit(false);
}
