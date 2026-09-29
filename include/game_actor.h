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
#include "scorer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_MAX_SUBSCRIBERS 2U

#define GAME_EVENTS_PER_MESSAGE ((2U * SCORER_MAX_EVENTS) + 1U)
#define GAME_OUTBOX_CAPACITY (1U + (GAME_MAX_SUBSCRIBERS * GAME_EVENTS_PER_MESSAGE))

typedef struct {
    Message items[GAME_OUTBOX_CAPACITY];
    uint8_t count;
} GameOutbox;

typedef struct {
    Pins pins[SCORER_MAX_BALLS];
    uint8_t count;
    GameStatus first_refused_for;
} HeldRolls;

typedef struct {
    ActorId ids[GAME_MAX_SUBSCRIBERS];
    uint8_t count;
} Subscribers;

typedef struct {
    ActorId id;
    Scorer scorer;
    Subscribers subscribers;
    HeldRolls held;
    uint16_t lost_to_full_queue;
    uint16_t lost_to_full_held_list;
    uint16_t not_understood;
} GameActor;

/* The game at `id`, which its events come from. */
void GameActor_Init(GameActor *self, ActorId id, const ScorerRules *rules);

/* Handles one message. The outbox is emptied first, then filled. */
void GameActor_Handle(GameActor *self, const Message *message, GameOutbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_H */
