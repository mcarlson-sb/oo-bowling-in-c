#ifndef GAME_SHELL_H
#define GAME_SHELL_H

/* The RTOS shell: the one game actor, in a task of its own, fed by queues. A request's reply
 * address is the caller's own FreeRTOS queue of GameOutput, and so is a subscriber's. */

#include "FreeRTOS.h"
#include "queue.h"

#include "game_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

void GameShell_Start(ScorerVariant variant, CountRule rule, UBaseType_t priority);

BaseType_t GameShell_Send(const GameMessage *message, TickType_t wait);

/* From the pinsetter's interrupt: a roll it counted. */
void GameShell_PinsetterCountedFromIsr(Pins pins);

/* Replies and events not delivered, so far, because the queue they were for was full. */
uint16_t GameShell_OutputsDropped(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_SHELL_H */
