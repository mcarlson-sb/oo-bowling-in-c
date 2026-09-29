#ifndef POSIX_STACK_H
#define POSIX_STACK_H

/* The POSIX port's stand-in for a task's stack high-water mark. There, a task runs on a pthread
 * stack of the port's own, not the buffer FreeRTOS was given, so uxTaskGetStackHighWaterMark
 * measures nothing. One task at a time: the one that painted last. */

#include <stddef.h>

/* From the task, first thing: paints its stack below the caller's frame. */
void PosixStack_Paint(void);

/* The deepest the painted task's stack has gone, in bytes below the point it painted from. */
size_t PosixStack_DeepestUse(void);

#endif /* POSIX_STACK_H */
