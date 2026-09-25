#ifndef SPARE_FRAME_H
#define SPARE_FRAME_H

/* A frame whose rolls knocked down all ten pins. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: SpareFrame "extends" Frame */
} SpareFrame;

/* Takes a copy of the rolls made so far, because this state lives in its own storage. */
Frame *SpareFrame_Init(SpareFrame *self, struct FrameContext *context, const uint8_t *rolls,
                       uint8_t roll_count);

#endif /* SPARE_FRAME_H */
