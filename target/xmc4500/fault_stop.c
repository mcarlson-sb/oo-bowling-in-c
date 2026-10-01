#include "fault.h"

#include "device.h"

/* The firmware's fail-stop on the XMC4500: masks every interrupt, so nothing runs again, and
 * waits, with the reason kept for a debugger to read. What the bike does then, its outputs' safe
 * state and the watchdog that resets it, is the hardware stage's to wire. Linked as an object, so
 * the host's in src/support stays out. */
_Noreturn void Fault_Stop(const char *reason)
{
    __disable_irq();
    __asm__ volatile("" : : "r"(reason));
    for (;;) {
        __WFI();
    }
}
