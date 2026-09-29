#ifndef RUNNING_AVERAGE_H
#define RUNNING_AVERAGE_H

/* A running average: an actor that hears a game's FRAME_CHANGED events as a subscriber, and
 * answers QUERY_SCORE, the selector the game and the scoreboard answer with a total, with the
 * average score of the complete frames, rounded down. Kay-oo's no-tap average listener, as an
 * actor. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_id.h"
#include "message.h"
#include "outbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Its state is its own: only the shell, which hosts it, and the tests see inside. */
typedef struct RunningAverage RunningAverage;

void RunningAverage_Init(RunningAverage *self, ActorId id);

/* Handles one message. The outbox is filled from where it stands. */
void RunningAverage_Handle(RunningAverage *self, const Message *message, Outbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* RUNNING_AVERAGE_H */
