/* The system calls the test image's C and C++ libraries need, which the firmware never links:
 * a heap for the host tests' std::vector, and an exit that ends QEMU. newlib's other calls are
 * nosys.specs's stubs, which fail. */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "semihosting.h"

#define TEST_HEAP_BYTES (16U * 1024U)

void *_sbrk(ptrdiff_t increment);
void _exit(int status);

/* In .bss, as any static: the firmware's link has no heap section to find, and none of this. */
__attribute__((aligned(8))) static uint8_t s_heap[TEST_HEAP_BYTES];
static size_t s_heap_used;

void *_sbrk(ptrdiff_t increment)
{
    if ((increment < 0) || ((size_t)increment > (TEST_HEAP_BYTES - s_heap_used))) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *grown = &s_heap[s_heap_used];
    s_heap_used += (size_t)increment;
    return grown;
}

/* abort(), and a failed allocation's std::terminate, end here. */
void _exit(int status)
{
    (void)status;
    Semihosting_Write("FAULT: the C library exited\n");
    Semihosting_Exit(false);
}
