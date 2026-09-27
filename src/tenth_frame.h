#ifndef TENTH_FRAME_H
#define TENTH_FRAME_H

/* A strike or spare in the tenth frame: there is no next frame, so it keeps its fill balls. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: TenthStrikeFrame "extends" Frame */
} TenthStrikeFrame;

typedef struct {
    Frame base; /* must be first: TenthSpareFrame "extends" Frame */
} TenthSpareFrame;

/* Two fill balls. */
Frame *TenthStrikeFrame_Init(TenthStrikeFrame *self);

/* One fill ball. */
Frame *TenthSpareFrame_Init(TenthSpareFrame *self, const Frame *replaced, Pins completing_pins);

#endif /* TENTH_FRAME_H */
