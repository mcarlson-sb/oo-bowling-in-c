#ifndef STRIKE_FRAME_H
#define STRIKE_FRAME_H

/* A frame whose first roll knocked down all ten pins (Bowling-OO strike-frame.ts). */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: StrikeFrame "extends" Frame */
} StrikeFrame;

Frame *StrikeFrame_Init(StrikeFrame *self, struct FrameContext *context);

#endif /* STRIKE_FRAME_H */
