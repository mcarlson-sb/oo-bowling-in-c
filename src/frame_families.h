#ifndef FRAME_FAMILIES_H
#define FRAME_FAMILIES_H

/* The two families of frame states (Abstract Factory): what a strike or a spare becomes. */

#include "bowling_types.h"
#include "frame.h"

struct FrameContext;

struct FrameStateFactory {
    Frame *(*new_strike)(struct FrameContext *context);
    Frame *(*new_spare)(struct FrameContext *context, const Frame *replaced, Pins completing_pins);
};

/* Frames 1 to 9: a strike or spare passes its bonus rolls on. */
extern const struct FrameStateFactory FrameFamily_Passing;

/* The tenth frame: a strike or spare keeps its fill balls. */
extern const struct FrameStateFactory FrameFamily_Tenth;

#endif /* FRAME_FAMILIES_H */
