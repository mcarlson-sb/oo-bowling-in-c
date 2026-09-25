#ifndef TENTH_FRAME_H
#define TENTH_FRAME_H

/* The last frame of a game: a strike or spare here earns fill balls, rolled within this same
 * frame. There is no frame after it, so it keeps every roll it takes. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: TenthFrame "extends" Frame */
} TenthFrame;

Frame *TenthFrame_Init(TenthFrame *self);

#endif /* TENTH_FRAME_H */
