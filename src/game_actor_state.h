#ifndef GAME_ACTOR_STATE_H
#define GAME_ACTOR_STATE_H

/* The game actor's state: private to the library, the shell that hosts it and the tests. The
 * include path is what keeps it private: only those targets have src/ on theirs. */

#include <stdint.h>

#include "game_actor.h"
#include "held_rolls.h"
#include "subscribers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the game's lifecycle is: which messages mean something now. Whether a game in play is
 * over is the scorer's to say. */
typedef enum {
    GAME_AWAITING_RULES,
    GAME_IN_PLAY,
    GAME_LIFECYCLE_STATES /* how many there are */
} GameLifecycle;

struct GameActor {
    ActorId id;
    GameLifecycle lifecycle;
    Scorer scorer;
    Subscribers subscribers;
    HeldRolls held;
    uint16_t lost_to_full_queue;
    uint16_t not_understood;
};

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_STATE_H */
