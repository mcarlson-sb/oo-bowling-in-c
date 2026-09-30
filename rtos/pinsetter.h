#ifndef PINSETTER_H
#define PINSETTER_H

/* The pinsetter: a feeder bound to one game's id. Its interrupt counts each roll into a queue, and
 * a full queue's losses into a one-slot report, and wakes the task that hosts its game. That task
 * takes them as messages from no one, to the game, before anything in its mailbox. Private to the
 * shell; the interrupt side has a file of its own, pinsetter_isr.c, so that its stack tripwire can
 * be the interrupt's. */

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "game_shell.h"
#include "message.h"

#define PINSETTER_ROLLS 32U

_Static_assert(PINSETTER_ROLLS >= SCORER_MAX_BALLS,
               "the pinsetter's queue holds a whole game of rolls");

typedef struct {
    ActorId game;
    TaskHandle_t task; /* the task that hosts the game, which the interrupt wakes */
    QueueHandle_t rolls;
    StaticQueue_t rolls_queue;
    uint8_t rolls_storage[PINSETTER_ROLLS * sizeof(Pins)];
    QueueHandle_t lost_report;
    StaticQueue_t lost_report_queue;
    uint8_t lost_report_storage[sizeof(uint16_t)];
    uint16_t lost_to_full_queue; /* the interrupt's own: nothing else touches it */
} Pinsetter;

/* Before the scheduler starts: feeding the game at `game`, hosted by `task`, with nothing lost. */
void Pinsetter_Start(Pinsetter *self, ActorId game, TaskHandle_t task);

/* Gives the task that hosts the pinsetter's game the next roll, or else the count of those lost,
 * as a message to the game. Returns false if neither waits. */
bool Pinsetter_Take(Pinsetter *self, Message *message);

/* From the interrupt: a roll counted. The newest is the one lost to a full queue. */
void Pinsetter_CountedFromIsr(Pinsetter *self, Pins pins);

/* The pinsetter of a lane, which its interrupt feeds. Stops, as a fault, for a lane no game is
 * hosted at. */
Pinsetter *GameShell_PinsetterOfLane(GameShellLane lane);

#endif /* PINSETTER_H */
