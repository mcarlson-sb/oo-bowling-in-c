#ifndef GAME_SHELL_H
#define GAME_SHELL_H

/* The RTOS shell, an actor host: where the kinds' instances, the tasks that host them, the
 * routing table and the pinsetter are created and wired. Actors address each other by id. The
 * routing table binds each id to a kind and an instance, in a hosting task with a mailbox, or to
 * the queue of an external actor that reads its own. A message goes to whoever is bound at its
 * "to" when it is sent, and the kind bound there decides what it means. */

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
 * GAME_SHELL_GAME_ID. A route is 24 bytes on this host, bound or not. */
#define GAME_SHELL_ACTORS 16U
#define GAME_SHELL_GAME_ID 1U

/* What sits at an id, which decides what a message sent there means. An external actor reads
 * its own queue, outside the shell; the kinds the shell hosts are dispatched by their tasks. */
typedef enum {
    ACTOR_KIND_NONE,
    ACTOR_KIND_EXTERNAL,
    ACTOR_KIND_GAME,
    ACTOR_KIND_SCOREBOARD,
    ACTOR_KIND_RUNNING_AVERAGE
} ActorKind;

/* The game at GAME_SHELL_GAME_ID, in a task of its own at `game_priority`, with no game yet: the
 * first NEW_GAME sent there starts one. And the task every observer will share, at
 * `observer_priority`: above the game's, it takes each event as the game sends it. */
void GameShell_Start(UBaseType_t game_priority, UBaseType_t observer_priority);

/* After GameShell_Start, before the scheduler starts: another game at `id`, a lane of its own,
 * in a task of its own at `priority`, with no game yet. The pinsetter's interrupt feeds only the
 * game at GAME_SHELL_GAME_ID. The observers' task must outrank it too. */
void GameShell_HostGame(ActorId id, UBaseType_t priority);

/* After GameShell_Start, before the scheduler starts: a scoreboard, or a running average, at
 * `id`, hosted by the observers' task. */
void GameShell_HostScoreboard(ActorId id);

void GameShell_HostRunningAverage(ActorId id);

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
