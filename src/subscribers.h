#ifndef SUBSCRIBERS_H
#define SUBSCRIBERS_H

/* Who a game tells about its frames, by id only: it never knows what they are. A plain value,
 * kept by the game, of at most GAME_MAX_SUBSCRIBERS. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_id.h"
#include "game_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ActorId ids[GAME_MAX_SUBSCRIBERS];
    uint8_t count;
} Subscribers;

void Subscribers_Init(Subscribers *self);

bool Subscribers_IsFull(const Subscribers *self);

uint8_t Subscribers_Count(const Subscribers *self);

/* A subscriber's id, by index, 0 up to the count. */
ActorId Subscribers_At(const Subscribers *self, uint8_t index);

void Subscribers_Add(Subscribers *self, ActorId id);

/* False, changing nothing, if id isn't one of them. */
bool Subscribers_Remove(Subscribers *self, ActorId id);

#ifdef __cplusplus
}
#endif

#endif /* SUBSCRIBERS_H */
