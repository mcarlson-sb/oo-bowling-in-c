#include "task_stack.h"

#include <stdint.h>
#include <string.h>

#include "port_stack.h"

#define TASK_STACK_PAINT 0xA5U
/* Left unpainted just below the painter, for memset's own frame. */
#define TASK_STACK_GUARD 512U

void TaskStack_Paint(TaskStack *self, const void *given)
{
    volatile uint8_t here = 0U;
    self->lowest = PortStack_Lowest(given);
    self->painted_from = (uintptr_t)&here;
    memset((void *)self->lowest, TASK_STACK_PAINT,
           (self->painted_from - TASK_STACK_GUARD) - self->lowest);
}

/* Reads another thread's stack while it is blocked: ThreadSanitizer can't see what orders it. */
__attribute__((no_sanitize_thread)) size_t TaskStack_DeepestUse(const TaskStack *self)
{
    uintptr_t touched = self->lowest;
    while ((touched < self->painted_from) && (*(const uint8_t *)touched == TASK_STACK_PAINT)) {
        touched++;
    }
    return (size_t)(self->painted_from - touched);
}
