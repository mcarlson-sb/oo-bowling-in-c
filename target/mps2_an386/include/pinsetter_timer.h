#ifndef PINSETTER_TIMER_H
#define PINSETTER_TIMER_H

/* The pinsetters' interrupt, on QEMU: the CMSDK timer 0, whose interrupt counts the next roll of
 * a script at each tick, at DEVICE_PINSETTER_PRIORITY, through ActorHost_PinsetterCountedFromIsr,
 * as the board's pinsetter input will. A real interrupt, so the FromISR calls run in handler mode
 * against the kernel's masking, as they will on the XMC4500. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_host.h"

/* One roll the script counts, at a lane. */
typedef struct {
    ActorHostLane lane;
    Pins pins;
} PinsetterTimerRoll;

/* Counts `count` rolls of `script`, which must outlive them, one every `period_us`. */
void PinsetterTimer_Start(const PinsetterTimerRoll *script, uint8_t count, uint32_t period_us);

/* Whether every roll of the script has been counted. */
bool PinsetterTimer_IsDone(void);

#endif /* PINSETTER_TIMER_H */
