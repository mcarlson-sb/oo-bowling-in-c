#ifndef ROUTER_H
#define ROUTER_H

/* The routing table: which kind and instance sits at each id, and the mailbox and task of the
 * task that hosts it, or the queue of an external actor. It posts a message to whoever is bound
 * at its "to", and counts what it couldn't. The host wires it. Written before the scheduler
 * starts, and only read after, so it needs no lock. */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "actor_id.h"
#include "actor_kind.h"
#include "outbox.h"

/* A row of the table. For an external actor, only the queue it reads: no task to wake. */
typedef struct {
    ActorKind kind;
    uint8_t instance;
    QueueHandle_t mailbox;
    TaskHandle_t task;
} Route;

typedef struct {
    Route routes[ROUTER_IDS];
    atomic_uint_least16_t dropped; /* written by the hosting tasks, read by any */
} Router;

/* Every id unbound, and nothing dropped. */
void Router_Reset(Router *self);

void Router_Bind(Router *self, ActorId id, Route route);

/* The route at `id`: an unbound one for an id past the table. */
const Route *Router_RouteTo(const Router *self, ActorId id);

/* Into the mailbox of whoever is bound at the message's "to", waking the task that hosts it.
 * False if nothing there takes messages, or its mailbox stayed full for `wait`. */
bool Router_Post(const Router *self, const Message *message, TickType_t wait);

/* Everything in the outbox, without waiting, counting each one that isn't taken. */
void Router_Deliver(Router *self, const Outbox *outbox);

void Router_CountDropped(Router *self);

uint16_t Router_Dropped(const Router *self);

#endif /* ROUTER_H */
