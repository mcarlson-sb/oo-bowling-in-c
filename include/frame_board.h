#ifndef FRAME_BOARD_H
#define FRAME_BOARD_H

/* A game's frames as a subscriber learns them from FRAME_CHANGED events: each one's score, and
 * whether it is complete. A plain value, kept by the kinds that listen to a game. */

#include <stdbool.h>
#include <stdint.h>

#include "scorer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Score scores[SCORER_MAX_FRAMES];
    bool complete[SCORER_MAX_FRAMES];
} FrameBoard;

void FrameBoard_Init(FrameBoard *self);

/* A frame's news. A frame number outside those kept is ignored. */
void FrameBoard_Hear(FrameBoard *self, const FrameEvent *frame);

/* The total of the complete frames. */
Score FrameBoard_Total(const FrameBoard *self);

uint8_t FrameBoard_CompleteCount(const FrameBoard *self);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_BOARD_H */
