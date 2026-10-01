#ifndef PORT_STACK_H
#define PORT_STACK_H

/* Where the calling task's stack ends, on this port: the lowest address of its pthread's stack,
 * which the POSIX port allocates itself, whatever buffer FreeRTOS was `given`. */

#include <stdint.h>

uintptr_t PortStack_Lowest(const void *given);

#endif /* PORT_STACK_H */
