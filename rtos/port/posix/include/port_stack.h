#ifndef PORT_STACK_H
#define PORT_STACK_H

/* Where the calling task's stack ends, on this port: the lowest address of its pthread's stack,
 * which the POSIX port allocates itself. */

#include <stdint.h>

uintptr_t PortStack_Lowest(void);

#endif /* PORT_STACK_H */
