#ifndef ACTOR_KIND_H
#define ACTOR_KIND_H

/* What the routing table binds an id to, and how many ids it has: the part of the router the
 * host's own users see. */

/* The ids the table can bind: 1 to ROUTER_IDS - 1. A route is 24 bytes on this host, bound or
 * not. */
#define ROUTER_IDS 16U

/* What sits at an id, which decides what a message sent there means. An external actor reads
 * its own queue, outside the shell; the kinds the shell hosts are dispatched by their tasks. */
typedef enum {
    ACTOR_KIND_NONE,
    ACTOR_KIND_EXTERNAL,
    ACTOR_KIND_GAME,
    ACTOR_KIND_SCOREBOARD,
    ACTOR_KIND_RUNNING_AVERAGE
} ActorKind;

#endif /* ACTOR_KIND_H */
