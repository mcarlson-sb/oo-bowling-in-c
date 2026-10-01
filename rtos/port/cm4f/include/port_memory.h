#ifndef PORT_MEMORY_H
#define PORT_MEMORY_H

/* Where the actor host's memory goes on this port: its statics, its tasks' stacks and queues,
 * and the idle task's, all in .bss.shell, which each image's linker script places by name in one
 * RAM region, before the rest of .bss, and its startup zeroes. */
#define PORT_SHELL_MEMORY __attribute__((section(".bss.shell")))

#endif /* PORT_MEMORY_H */
