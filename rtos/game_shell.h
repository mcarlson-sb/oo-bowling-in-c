#ifndef GAME_SHELL_H
#define GAME_SHELL_H

/* The RTOS shell: the one game actor, in a task of its own, fed by queues. Actors address each
 * other by id; the shell's routing table binds each id to the FreeRTOS queue of Message its
 * actor reads, so a reply or an event goes to whoever is bound at its "to" when it is sent. */

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "game_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ENG-1.3's stack budgets, in bytes, on this host: the game task's, the interrupt side's, and
 * what GameShell_Send costs its caller. tools/stack_depth.py checks each against the static call
 * graph, deepest path plus a host allowance (RTOS_ACTOR.md), and the painted stack checks the
 * game task's. */
#define GAME_SHELL_TASK_STACK_BUDGET 4608U
#define GAME_SHELL_ISR_STACK_BUDGET 3584U
#define GAME_SHELL_SEND_STACK_BUDGET 3584U

/* The ids the routing table can bind: 1 to GAME_SHELL_ACTORS - 1. The game is at
 * GAME_SHELL_GAME_ID. */
#define GAME_SHELL_ACTORS 8U
#define GAME_SHELL_GAME_ID 1U

/* What sits at an id, which decides what a message sent there means. An external actor reads
 * its own queue, outside the shell; the shell's own kinds are dispatched by its tasks. */
typedef enum {
    ACTOR_KIND_NONE,
    ACTOR_KIND_EXTERNAL,
    ACTOR_KIND_GAME
} ActorKind;

void GameShell_Start(const ScorerRules *rules, UBaseType_t priority);

/* After GameShell_Start, before the scheduler starts: an external actor at `id`, which reads
 * `queue`. */
void GameShell_Bind(ActorId id, QueueHandle_t queue);

/* To whoever is bound at the message's "to". */
BaseType_t GameShell_Send(const Message *message, TickType_t wait);

/* From the pinsetter's interrupt: a roll it counted. */
void GameShell_PinsetterCountedFromIsr(Pins pins);

/* Replies and events not delivered, so far: to an id nothing is bound to, or can be, or to a
 * full queue. */
uint16_t GameShell_OutputsDropped(void);

/* The deepest the game task's stack has gone, in bytes, measured by painting it. */
size_t GameShell_TaskStackUsed(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_SHELL_H */
