#ifndef PINSETTER_H
#define PINSETTER_H

/* The pinsetter: the machine at the end of the lane that counts the pins that fell. In
 * firmware it reports each roll from an interrupt handler, at any moment, while the main loop
 * may be in the middle of anything.
 *
 * So the pinsetter never touches a game. It only posts pins to its own mailbox, and the main
 * loop drains that mailbox into the game when it is ready. All of the game's logic, and every
 * listener, then runs on the main thread.
 *
 * One thread posts and one drains: that is what lets the mailbox work without a lock. So only
 * the interrupt side may call Pinsetter_Post, never the main loop, or a listener. A listener
 * that wants to roll calls Game_Roll, and the game queues the roll in its own mailbox. */

#include <stdbool.h>

#include "bowling_types.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pinsetter Pinsetter;

/* An empty pinsetter. NULL if none is free: they come from a fixed pool, like games. */
Pinsetter *Pinsetter_Create(void);

void Pinsetter_Destroy(Pinsetter *pinsetter);

/* Interrupt side. Posts the pins that fell in one roll. Returns false, posting nothing, if
 * the mailbox is full: 21 rolls, a whole game's, are waiting that the main loop hasn't
 * drained. */
bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins);

/* Main-loop side. Rolls every posted roll into `game`, oldest first, with Game_Roll, and
 * returns GAME_OK once they have all gone in.
 *
 * A roll the game rejects is never thrown away: the machine reported it, and it is often the
 * right one, made to look impossible by an earlier roll that was miscounted. So draining stops
 * there and returns that roll's status (such as GAME_ERR_INVALID_PINS), leaving it, and every
 * roll after it, waiting. The scorer resolves it, for instance by correcting the earlier roll
 * with Game_CorrectRoll, and the next drain carries on from it. */
GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game);

/* Main-loop side. Throws away the oldest waiting roll: the one a drain stopped at, when the
 * scorer decides the machine got it wrong. Returns false if no roll is waiting. */
bool Pinsetter_DiscardOldest(Pinsetter *pinsetter);

#ifdef __cplusplus
}
#endif

#endif /* PINSETTER_H */
