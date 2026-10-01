#ifndef ACTOR_HOST_H
#define ACTOR_HOST_H

/* The RTOS shell, an actor host: where the kinds' instances, the tasks that host them, the
 * routing table and the lanes' pinsetters are created and wired. Actors address each other by id.
 * The routing table binds each id to a kind and an instance, in a hosting task with a mailbox, or
 * to the queue of an external actor that reads its own. A message goes to whoever is bound at its
 * "to" when it is sent, and the kind bound there decides what it means. */

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "actor_kind.h"
#include "game_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ENG-1.3's stack budgets, in bytes, on this host: the game task's, the interrupt side's, and
 * what ActorHost_Send costs its caller. tools/stack_depth.py checks each against the static call
 * graph, deepest path plus a host allowance (RTOS_ACTOR.md), and the painted stack checks the
 * game task's. */
#define ACTOR_HOST_TASK_STACK_BUDGET 4608U
#define ACTOR_HOST_ISR_STACK_BUDGET 3584U
#define ACTOR_HOST_SEND_STACK_BUDGET 3584U

/* The game is at ACTOR_HOST_GAME_ID; the ids the routing table can bind are 1 to ROUTER_IDS - 1. */
#define ACTOR_HOST_GAME_ID 1U

/* The game at ACTOR_HOST_GAME_ID, in a task of its own at `game_priority`, with no game yet: the
 * first NEW_GAME sent there starts one. And the task every observer will share, at
 * `observer_priority`: above the game's, it takes each event as the game sends it. */
void ActorHost_Start(UBaseType_t game_priority, UBaseType_t observer_priority);

/* After ActorHost_Start, before the scheduler starts: another game at `id`, the next lane, in a
 * task of its own at `priority` and fed by a pinsetter of its own, with no game yet. The
 * observers' task must outrank it too. */
void ActorHost_HostGame(ActorId id, UBaseType_t priority);

/* After ActorHost_Start, before the scheduler starts: a scoreboard, or a running average, at
 * `id`, hosted by the observers' task. */
void ActorHost_HostScoreboard(ActorId id);

void ActorHost_HostRunningAverage(ActorId id);

/* After ActorHost_Start, before the scheduler starts: an external actor at `id`, which reads
 * `queue`. */
void ActorHost_Bind(ActorId id, QueueHandle_t queue);

/* To whoever is bound at the message's "to". */
BaseType_t ActorHost_Send(const Message *message, TickType_t wait);

/* A lane is a hosted game, counted from 0 in the order the games were hosted: lane 0 is the game
 * at ACTOR_HOST_GAME_ID. */
typedef uint8_t ActorHostLane;

/* From a lane's pinsetter interrupt: a roll it counted. Stops, as a fault, for a lane no game is
 * hosted at. */
void ActorHost_PinsetterCountedFromIsr(ActorHostLane lane, Pins pins);

/* Replies and events not delivered, so far: to an id nothing is bound to, or can be, or to a
 * full queue. */
uint16_t ActorHost_OutputsDropped(void);

/* The deepest the game task's stack has gone, in bytes, measured by painting it. */
size_t ActorHost_TaskStackUsed(void);

#ifdef __cplusplus
}
#endif

#endif /* ACTOR_HOST_H */
