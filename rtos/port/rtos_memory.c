#include "FreeRTOS.h"
#include "task.h"

#include "port_memory.h"

/* The idle task's memory. With no heap, FreeRTOS asks for it through this hook instead of
 * allocating it. */
static StaticTask_t s_idle_task PORT_SHELL_MEMORY;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE] PORT_SHELL_MEMORY;

void vApplicationGetIdleTaskMemory(StaticTask_t **task, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *stack_depth)
{
    *task = &s_idle_task;
    *stack = s_idle_stack;
    *stack_depth = configMINIMAL_STACK_SIZE;
}
