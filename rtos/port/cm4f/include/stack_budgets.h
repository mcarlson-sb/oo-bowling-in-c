#ifndef STACK_BUDGETS_H
#define STACK_BUDGETS_H

#include "FreeRTOS.h"

/* ENG-1.3's stack budgets on this port, the Cortex-M4F, in bytes: the actor host's hosting task's,
 * its interrupt side's, and what ActorHost_Send costs its caller. tools/stack_depth.py checks each
 * against the static call graph, deepest path plus the target's allowances: no C library under
 * the port, and one exception frame (RTOS_ACTOR.md). */
#define ACTOR_HOST_TASK_STACK_BUDGET 2048U
#define ACTOR_HOST_ISR_STACK_BUDGET 1024U
#define ACTOR_HOST_SEND_STACK_BUDGET 1024U

/* A hosting task's stack, in words: its budget, which is all it runs on. */
#define ACTOR_HOST_TASK_STACK_WORDS (ACTOR_HOST_TASK_STACK_BUDGET / sizeof(StackType_t))

#endif /* STACK_BUDGETS_H */
