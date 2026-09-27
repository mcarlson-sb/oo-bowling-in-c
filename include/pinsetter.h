#ifndef PINSETTER_H
#define PINSETTER_H

/* The pinsetter: the machine at the end of the lane that counts the pins that fell. In
 * firmware it reports each roll from an interrupt handler, at any moment, while the main loop
 * may be in the middle of anything.
 *
 * So the pinsetter never touches a game. It only posts pins to its own mailbox, and the main
 * loop drains that mailbox into the game when it is ready. All of the game's logic, and every
 * listener, then runs on the main thread. */

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
 * the mailbox is full: 8 rolls are waiting that the main loop hasn't drained. */
bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins);

/* Main-loop side. Rolls every posted roll into `game`, oldest first, with Game_Roll. */
void Pinsetter_Drain(Pinsetter *pinsetter, Game *game);

#ifdef __cplusplus
}
#endif

#endif /* PINSETTER_H */
