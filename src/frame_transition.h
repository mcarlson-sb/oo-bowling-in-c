#ifndef FRAME_TRANSITION_H
#define FRAME_TRANSITION_H

/* The part of FrameContext a state needs in order to change state, and nothing else.
 *
 * FrameContext has two kinds of client. Game asks it to roll, score and report; that side
 * is in frame_context.h. A state asks it for the next state and switches to it; that side
 * is here (Interface Segregation). So RegularFrame depends only on these three functions,
 * and Game never sees them. The context's layout isn't needed, so it's only
 * forward-declared.
 *
 * Private to the library: lives in src/, not include/. */

#include "frame.h"

struct FrameContext;

/* The next state, from this context's family (frames 1 to 9, or the tenth). Each builds the
 * state in the context's own storage and returns it, ready for FrameContext_SetState. */
Frame *FrameContext_NewStrikeFrame(struct FrameContext *self);
Frame *FrameContext_NewSpareFrame(struct FrameContext *self, const Frame *replaced,
                                  Pins completing_pins);

/* Makes frame_state the context's current state. */
void FrameContext_SetState(struct FrameContext *self, Frame *frame_state);

#endif /* FRAME_TRANSITION_H */
