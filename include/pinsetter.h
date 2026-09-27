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
#include <stdint.h>

#include "bowling_types.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pinsetter Pinsetter;

/* An empty pinsetter, from a fixed pool. Never NULL: a lane's pinsetter is fixed when the
 * system is built, so running out of them is a configuration error, and Pinsetter_Create
 * stops the program (Fault_Stop, in every build) rather than return one.
 *
 * Every function below takes a pinsetter from Pinsetter_Create. None of them checks for
 * NULL: that is a precondition, not a case they handle. */
Pinsetter *Pinsetter_Create(void);

/* Detach the interrupt handler before calling this: a post to a pinsetter already given back
 * to the pool would land in whichever pinsetter is created next. The pool can't check that,
 * so it is the caller's to keep. */
void Pinsetter_Destroy(Pinsetter *pinsetter);

/* Interrupt side. Posts the pins that fell in one roll.
 *
 * One producer only: exactly one interrupt handler may post to a pinsetter, never the main
 * loop, a listener, or a second handler, and never a nested interrupt while a post is under
 * way. Two posts at once can write the same slot and lose a roll. A debug build stops the
 * program (Fault_Stop) if it catches two posts overlapping; that is a net, not a proof, since
 * posts that happen not to overlap aren't caught.
 *
 * Returns false, posting nothing, if
 * the mailbox is full: 21 rolls, a whole game's, are waiting that the main loop hasn't
 * drained.
 *
 * The 21 are enough for every roll of one game while a drain is stopped, but only if every
 * waiting roll is a roll of that game. A glitch waiting to be discarded takes a slot too, and
 * so does a roll of the game before that is still waiting for the scorer: then the game's
 * last roll can be refused. That is accepted, not sized for: Pinsetter_RollsLost counts it,
 * so it is never silent. */
bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins);

/* Main-loop side. Rolls every roll waiting when it starts into `game`, oldest first, with
 * Game_Roll, and returns GAME_OK once they have all gone in. A roll posted while it runs
 * waits for the next drain, so one drain does at most a mailbox's worth of work.
 *
 * A roll the game rejects is never thrown away: the machine reported it, and it is often the
 * right one, made to look impossible by an earlier roll that was miscounted. So draining stops
 * there and returns that roll's status (such as GAME_ERR_INVALID_PINS), leaving it, and every
 * roll after it, waiting. The scorer resolves it, for instance by correcting the earlier roll
 * with Game_CorrectRoll, and the next drain carries on from it.
 *
 * Not from inside a frame-changed callback: there the game refuses the first waiting roll
 * with GAME_ERR_DURING_NOTIFICATION, like any roll from a callback, so the drain stops
 * with it still waiting. (With nothing waiting, there is nothing to refuse: GAME_OK.) */
GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game);

/* Main-loop side. Throws away the oldest waiting roll: the one a drain stopped at, when the
 * scorer decides the machine got it wrong. Returns false if no roll is waiting. */
bool Pinsetter_DiscardOldest(Pinsetter *pinsetter);

/* Main-loop side. How many rolls Pinsetter_Post has ever refused because the mailbox was
 * full. A query: asking changes nothing, so any number of readers can watch it. Each keeps
 * the last value it read and takes the difference in uint16_t,
 *     lost_since = (uint16_t)(Pinsetter_RollsLost(pinsetter) - last_seen);
 * which stays right when the count wraps from 65,535 to 0, as long as fewer than 65,536 are
 * lost between two reads.
 *
 * The mailbox holds a whole game's rolls, so in normal play this never moves. Any increase
 * is a real anomaly: the main loop stopped draining for a whole game or more, or the machine
 * reported rolls that no game has. Either way, rolls the bowler made are missing, and the
 * scorer needs to know. */
uint16_t Pinsetter_RollsLost(const Pinsetter *pinsetter);

#ifdef __cplusplus
}
#endif

#endif /* PINSETTER_H */
