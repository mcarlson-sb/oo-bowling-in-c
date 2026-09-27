#ifndef SPARE_FRAME_H
#define SPARE_FRAME_H

/* A frame whose rolls knocked down all ten pins. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: SpareFrame "extends" Frame */
} SpareFrame;

/* Copies the replaced frame's rolls, since each state has its own storage. */
Frame *SpareFrame_Init(SpareFrame *self, const Frame *replaced, Pins completing_pins);

#endif /* SPARE_FRAME_H */
