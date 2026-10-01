#ifndef SEMIHOSTING_H
#define SEMIHOSTING_H

/* ARM semihosting, which QEMU answers: the QEMU image's output, and its exit status. Each call is a
 * BKPT 0xAB that the debugger, or QEMU, services. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define SEMIHOSTING_NORETURN [[noreturn]]
#else
#define SEMIHOSTING_NORETURN _Noreturn
#endif

/* Writes a NUL-terminated string to QEMU's standard output. */
void Semihosting_Write(const char *text);

/* Writes `value` in decimal. */
void Semihosting_WriteNumber(uint32_t value);

/* Ends QEMU, which exits 0 if `passed`, or 1. */
SEMIHOSTING_NORETURN void Semihosting_Exit(bool passed);

#ifdef __cplusplus
}
#endif

#endif /* SEMIHOSTING_H */
