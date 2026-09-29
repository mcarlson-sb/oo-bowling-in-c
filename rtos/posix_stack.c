#define _GNU_SOURCE /* pthread_getattr_np */

#include "posix_stack.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define POSIX_STACK_PAINT 0xA5U
/* Left unpainted just below the painter, for memset's own frame. */
#define POSIX_STACK_GUARD 512U

void PosixStack_Paint(PosixStack *self)
{
    pthread_attr_t attributes;
    void *lowest = NULL;
    size_t size = 0U;
    volatile uint8_t here = 0U;
    (void)pthread_getattr_np(pthread_self(), &attributes);
    (void)pthread_attr_getstack(&attributes, &lowest, &size);
    (void)pthread_attr_destroy(&attributes);
    self->lowest = (uintptr_t)lowest;
    self->painted_from = (uintptr_t)&here;
    memset(lowest, POSIX_STACK_PAINT, (self->painted_from - POSIX_STACK_GUARD) - self->lowest);
}

/* Reads another thread's stack while it is blocked: ThreadSanitizer can't see what orders it. */
__attribute__((no_sanitize_thread)) size_t PosixStack_DeepestUse(const PosixStack *self)
{
    uintptr_t touched = self->lowest;
    while ((touched < self->painted_from) && (*(const uint8_t *)touched == POSIX_STACK_PAINT)) {
        touched++;
    }
    return (size_t)(self->painted_from - touched);
}
