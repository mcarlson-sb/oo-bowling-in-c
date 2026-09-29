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

typedef struct {
    ActorId id;
    Score scores[SCORER_MAX_FRAMES];
    bool complete[SCORER_MAX_FRAMES];
    uint16_t not_understood;
} Scoreboard;

void Scoreboard_Init(Scoreboard *self, ActorId id);

/* Handles one message. The outbox is filled from where it stands. */
void Scoreboard_Handle(Scoreboard *self, const Message *message, Outbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* SCOREBOARD_H */
