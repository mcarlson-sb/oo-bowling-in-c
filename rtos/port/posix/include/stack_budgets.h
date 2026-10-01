#ifndef STACK_BUDGETS_H
#define STACK_BUDGETS_H

#include "FreeRTOS.h"

/* ENG-1.3's stack budgets on this port, the POSIX host, in bytes: the actor host's hosting task's,
 * its interrupt side's, and what ActorHost_Send costs its caller. tools/stack_depth.py checks each
 * against the static call graph, deepest path plus the host's allowances (RTOS_ACTOR.md), and the
 * painted stack checks the game task's. */
#define ACTOR_HOST_TASK_STACK_BUDGET 4608U
#define ACTOR_HOST_ISR_STACK_BUDGET 3584U
#define ACTOR_HOST_SEND_STACK_BUDGET 3584U

/* A hosting task's stack, in words. The POSIX port sizes a task's pthread stack from this, and a
 * pthread stack is at least PTHREAD_STACK_MIN, so the port's minimum. */
#define ACTOR_HOST_TASK_STACK_WORDS configMINIMAL_STACK_SIZE

#endif /* STACK_BUDGETS_H */
