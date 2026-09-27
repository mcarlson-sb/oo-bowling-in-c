#ifndef PINSETTER_H
#define PINSETTER_H

/* The machine that counts the pins that fell. Its interrupt handler posts each roll to a
 * mailbox, and the main loop drains the mailbox into a game, so the game and its listeners
 * only ever run on the main loop. The mailbox is lock-free because it has one producer (the
 * interrupt side) and one consumer (the main loop). No function checks for a NULL pinsetter. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pinsetter Pinsetter;

/* From a fixed pool. Running out is a configuration error: it stops the program (Fault_Stop). */
Pinsetter *Pinsetter_Create(void);

/* Detach the interrupt handler first: a later post would land in the next pinsetter created.
 * NULL, or a pointer that isn't a pinsetter, is ignored; destroying one twice, or while it is
 * draining, stops the program (Fault_Stop). */
void Pinsetter_Destroy(Pinsetter *pinsetter);

/* Interrupt side, from exactly one handler. Overlapping posts could lose a roll; a debug
 * build stops the program (Fault_Stop) if it catches one. False, posting nothing and counting
 * the roll lost, if 21 rolls are already waiting. That is a whole game's, but a waiting glitch
 * or a roll of the game before also takes a slot. */
bool Pinsetter_Post(Pinsetter *pinsetter, Pins pins);

/* Main-loop side. Rolls the rolls waiting when it starts into the game, oldest first. Stops at
 * a roll the game rejects and returns its status, keeping that roll and the ones after it
 * waiting: it is often right, and an earlier roll wrong, so the scorer decides (with
 * Game_CorrectRoll or Pinsetter_DiscardOldest). From inside a drain: GAME_ERR_BUSY. */
GameStatus Pinsetter_Drain(Pinsetter *pinsetter, Game *game);

/* Main-loop side. False if nothing is waiting, or while a drain is running. */
bool Pinsetter_DiscardOldest(Pinsetter *pinsetter);

/* Main-loop side. How many posts have ever been refused; never reset, so several readers can
 * each keep their last reading, and take the difference in uint16_t to survive the wrap. */
uint16_t Pinsetter_RollsLost(const Pinsetter *pinsetter);

#ifdef __cplusplus
}
#endif

#endif /* PINSETTER_H */
