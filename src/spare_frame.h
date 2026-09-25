#ifndef SPARE_FRAME_H
#define SPARE_FRAME_H

/* A frame whose rolls knocked down all ten pins. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: SpareFrame "extends" Frame */
} SpareFrame;

/* Builds the spare from the frame it replaces: a copy of that frame's rolls, because this
 * state has its own storage, plus the roll that completed the spare. */
Frame *SpareFrame_Init(SpareFrame *self, const Frame *replaced, uint8_t completing_pins);

#endif /* SPARE_FRAME_H */
