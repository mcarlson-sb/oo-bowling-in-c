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

/* The decisions the game's lifecycle stores: a new game started it, and nothing the game holds
 * can work it out. Facts are derived, not stored: whether a game is over is the scorer's to say,
 * and whether it is holding rolls is the held list's. */
typedef enum {
    GAME_AWAITING_RULES,
    GAME_PRACTICE, /* a new game asked for it: balls are counted, not scored, until it ends */
    GAME_IN_PLAY
} GameLifecycle;

struct GameActor {
    ActorId id;
    GameLifecycle lifecycle;
    Scorer scorer;
    Subscribers subscribers;
    HeldRolls held;
    uint16_t lost_to_full_queue;
    uint16_t not_understood;
    uint16_t practice_balls; /* this game's */
    bool pinsetter_down;     /* a decision across the lifecycle: only the pinsetter's rolls heed it */
    uint16_t rolls_refused;  /* the pinsetter's, while it was down */
};

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_STATE_H */
