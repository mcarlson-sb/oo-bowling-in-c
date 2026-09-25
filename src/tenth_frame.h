#ifndef TENTH_FRAME_H
#define TENTH_FRAME_H

/* The states a strike or spare in the last frame moves to. The first two balls of the tenth
 * frame behave like any other frame's, so the tenth frame starts as a RegularFrame too. A
 * strike or spare earns fill balls, rolled within this same frame, and there is no frame
 * after it, so these states keep every roll they take. */

#include "frame.h"

typedef struct {
    Frame base; /* must be first: TenthStrikeFrame "extends" Frame */
} TenthStrikeFrame;

typedef struct {
    Frame base; /* must be first: TenthSpareFrame "extends" Frame */
} TenthSpareFrame;

/* A strike in the tenth frame: takes two fill balls. */
Frame *TenthStrikeFrame_Init(TenthStrikeFrame *self);

/* A spare in the tenth frame: takes one fill ball. Built like a SpareFrame, from the frame
 * it replaces plus the roll that completed the spare. */
Frame *TenthSpareFrame_Init(TenthSpareFrame *self, const Frame *replaced, Pins completing_pins);

#endif /* TENTH_FRAME_H */
