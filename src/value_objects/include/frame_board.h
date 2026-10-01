#ifndef FRAME_BOARD_H
#define FRAME_BOARD_H

/* A game's frames as a subscriber learns them from FRAME_CHANGED events: each one's score, and
 * whether it is complete. A plain value, kept by the kinds that listen to a game. */

#include <stdbool.h>
#include <stdint.h>

#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Score scores[BOWLING_MAX_FRAMES];
    bool complete[BOWLING_MAX_FRAMES];
} FrameBoard;

void FrameBoard_Init(FrameBoard *self);

/* Takes in a frame's news, and ignores a frame number outside those kept. */
void FrameBoard_Hear(FrameBoard *self, const FrameEvent *frame);

/* What the board knows, as facts a kind reports: how many frames are complete, and their total. */
typedef struct {
    uint8_t complete_frames;
    Score total;
} FrameBoardFacts;

FrameBoardFacts FrameBoard_Facts(const FrameBoard *self);

/* The total of the complete frames. */
Score FrameBoard_Total(const FrameBoard *self);

uint8_t FrameBoard_CompleteCount(const FrameBoard *self);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_BOARD_H */
