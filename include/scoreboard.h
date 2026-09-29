#ifndef SCOREBOARD_H
#define SCOREBOARD_H

/* A scoreboard: an actor that rebuilds a game's frames from the FRAME_CHANGED events it hears,
 * as a subscriber, and answers QUERY_SCORE with the total of those complete. It knows nothing of
 * the game but the protocol. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_id.h"
#include "message.h"
#include "outbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most one message makes it send: a reply, or a NOT_UNDERSTOOD. */
#define SCOREBOARD_MOST_SENT 1U

/* Its state is its own: only the shell, which hosts it, and the tests see inside. */
typedef struct Scoreboard Scoreboard;

void Scoreboard_Init(Scoreboard *self, ActorId id);

/* Handles one message. The outbox is filled from where it stands. */
void Scoreboard_Handle(Scoreboard *self, const Message *message, Outbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* SCOREBOARD_H */
