#ifndef GAME_ACTOR_H
#define GAME_ACTOR_H

/* The game actor: one game, and everything that may change it, as messages. It handles one
 * message at a time and writes what it sends, replies and events, to an outbox the caller
 * supplies. It knows nothing of the RTOS, and holds no pointers: it addresses actors by id,
 * and the RTOS shell, which owns the one instance, routes each id to a queue. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_id.h"
#include "message.h"
#include "outbox.h"
#include "scorer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_MAX_SUBSCRIBERS 2U

/* The most events one message makes the game tell a subscriber: an edit mid-game, when rolls can
 * be held, reopens at most the nine complete frames, then lets held rolls through that complete
 * all ten, and holds or loses the one after. */
#define GAME_EVENTS_PER_MESSAGE ((SCORER_MAX_EVENTS - 1U) + SCORER_MAX_EVENTS + 1U)
#define GAME_OUTBOX_CAPACITY (1U + (GAME_MAX_SUBSCRIBERS * GAME_EVENTS_PER_MESSAGE))


/* The game's state is its own: only the shell, which hosts it, and the tests see inside, through
 * src/game_actor_state.h. */
typedef struct GameActor GameActor;

/* The game at `id`, which its events come from. */
void GameActor_Init(GameActor *self, ActorId id);

/* Handles one message, adding what it sends to the outbox, which the host empties. */
void GameActor_Handle(GameActor *self, const Message *message, Outbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_H */
