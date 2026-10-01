#ifndef ACTOR_HOST_LANES_H
#define ACTOR_HOST_LANES_H

/* Private to the host: a lane's pinsetter, for the host's interrupt side, which has a file of its
 * own, actor_host_isr.c, so that its stack tripwire can be the interrupt's. */

#include "actor_host.h"
#include "pinsetter.h"

/* The pinsetter of a lane, which its interrupt feeds. Stops, as a fault, for a lane no game is
 * hosted at. */
Pinsetter *ActorHost_PinsetterOfLane(ActorHostLane lane);

#endif /* ACTOR_HOST_LANES_H */
