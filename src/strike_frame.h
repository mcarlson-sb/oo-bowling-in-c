#ifndef STRIKE_FRAME_H
#define STRIKE_FRAME_H

/* A frame whose first roll knocked down all ten pins. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: StrikeFrame "extends" Frame */
} StrikeFrame;

Frame *StrikeFrame_Init(StrikeFrame *self);

#endif /* STRIKE_FRAME_H */
