#ifndef REGULAR_FRAME_H
#define REGULAR_FRAME_H

/* The state every frame except the tenth starts in: rolls that are neither a strike nor a
 * spare. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: RegularFrame "extends" Frame */
} RegularFrame;

Frame *RegularFrame_Init(RegularFrame *self);

#endif /* REGULAR_FRAME_H */
