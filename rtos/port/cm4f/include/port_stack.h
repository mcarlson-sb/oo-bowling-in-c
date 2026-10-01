#ifndef PORT_STACK_H
#define PORT_STACK_H

/* Where the calling task's stack ends, on this port: the lowest address of the buffer FreeRTOS
 * was `given` for it, on which the task runs. */

#include <stdint.h>

uintptr_t PortStack_Lowest(const void *given);

#endif /* PORT_STACK_H */
