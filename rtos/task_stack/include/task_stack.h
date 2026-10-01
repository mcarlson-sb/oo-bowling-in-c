#ifndef TASK_STACK_H
#define TASK_STACK_H

/* A task's stack high-water mark, by painting, on any port. The task paints its own stack first
 * thing, below its entry's frame, so what it measures is the depth of the work it does. The port
 * says where the calling task's stack ends (port_stack.h): on the POSIX port, a task runs on a
 * pthread stack of the port's own, not the buffer FreeRTOS was given. */

#include <stddef.h>
#include <stdint.h>

/* One task's painted stack. */
typedef struct {
    uintptr_t lowest;
    uintptr_t painted_from;
} TaskStack;

/* From the task, first thing: paints its stack below the caller's frame. */
void TaskStack_Paint(TaskStack *self);

/* The deepest the painted task's stack has gone, in bytes below the point it painted from. */
size_t TaskStack_DeepestUse(const TaskStack *self);

#endif /* TASK_STACK_H */
