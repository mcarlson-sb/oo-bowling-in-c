#ifndef REGULAR_FRAME_H
#define REGULAR_FRAME_H

/* The state every frame starts in (Bowling-OO regular-frame.ts). */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: RegularFrame "extends" Frame */
} RegularFrame;

Frame *RegularFrame_Init(RegularFrame *self, struct FrameContext *context);

#endif /* REGULAR_FRAME_H */
