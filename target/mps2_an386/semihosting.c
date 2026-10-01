#include "semihosting.h"

#include <stddef.h>
#include <stdint.h>

/* The semihosting operations, and the reasons SYS_EXIT reports: QEMU exits 0 for an application
 * that exited, and 1 for any other reason. */
#define SEMIHOSTING_SYS_WRITE0 0x04U
#define SEMIHOSTING_SYS_EXIT 0x18U
#define SEMIHOSTING_APPLICATION_EXIT 0x20026U
#define SEMIHOSTING_RUN_TIME_ERROR 0x20023U

static uint32_t Semihosting_Call(uint32_t operation, uintptr_t argument)
{
    register uint32_t r0 __asm__("r0") = operation;
    register uintptr_t r1 __asm__("r1") = argument;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

void Semihosting_Write(const char *text)
{
    (void)Semihosting_Call(SEMIHOSTING_SYS_WRITE0, (uintptr_t)text);
}

void Semihosting_WriteNumber(uint32_t value)
{
    char digits[11];
    size_t at = sizeof digits - 1U;
    digits[at] = '\0';
    do {
        digits[--at] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    Semihosting_Write(&digits[at]);
}

_Noreturn void Semihosting_Exit(bool passed)
{
    (void)Semihosting_Call(SEMIHOSTING_SYS_EXIT,
                           passed ? SEMIHOSTING_APPLICATION_EXIT : SEMIHOSTING_RUN_TIME_ERROR);
    for (;;) {
        /* QEMU has gone: nothing runs after SYS_EXIT. */
    }
}
