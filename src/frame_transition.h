#ifndef FRAME_TRANSITION_H
#define FRAME_TRANSITION_H

/* A state's side of FrameContext (Interface Segregation): Game never sees these. */

#include "frame.h"

struct FrameContext;

/* Built from the context's family, in its own storage. */
Frame *FrameContext_NewStrikeFrame(struct FrameContext *self);
Frame *FrameContext_NewSpareFrame(struct FrameContext *self, const Frame *replaced,
                                  Pins completing_pins);

void FrameContext_SetState(struct FrameContext *self, Frame *frame_state);

#endif /* FRAME_TRANSITION_H */
