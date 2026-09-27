#ifndef REGULAR_FRAME_H
#define REGULAR_FRAME_H

/* The state every frame starts in, and stays in unless it becomes a strike or a spare. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: RegularFrame "extends" Frame */
} RegularFrame;

Frame *RegularFrame_Init(RegularFrame *self);

#endif /* REGULAR_FRAME_H */
