#ifndef SPARE_FRAME_H
#define SPARE_FRAME_H

/* A frame whose rolls knocked down all ten pins (Bowling-OO spare-frame.ts). */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: SpareFrame "extends" Frame */
    uint8_t bonus_rolls[1];
    uint8_t bonus_count;
} SpareFrame;

/* The TypeScript constructor keeps a reference to the RegularFrame's rolls array. Here the
 * rolls are copied, because this state lives in its own storage. */
Frame *SpareFrame_Init(SpareFrame *self, struct FrameContext *context, const uint8_t *rolls,
                       uint8_t roll_count);

#endif /* SPARE_FRAME_H */
