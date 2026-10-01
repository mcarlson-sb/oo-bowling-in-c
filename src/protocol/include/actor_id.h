#ifndef ACTOR_ID_H
#define ACTOR_ID_H

/* An actor's address. Who sits at an id is bound late, by the shell's routing table: senders
 * and the game know ids only. */

#include <stdint.h>

typedef uint8_t ActorId;

#define ACTOR_ID_NONE 0U

#endif /* ACTOR_ID_H */
