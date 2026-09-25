#ifndef FRAME_CONTEXT_H
#define FRAME_CONTEXT_H

/* The State-pattern context: one per frame. Holds the frame's current state and forwards
 * each call to it.
 *
 * Every frame starts as a RegularFrame. What a strike or spare becomes depends on where the
 * frame is: in frames 1 to 9 it passes its bonus rolls on to the next frame, and in the
 * tenth it keeps its fill balls. So each context is built with a family of states, an
 * Abstract Factory, and RegularFrame just asks its context for "the strike state" or "the
 * spare state".
 *
 * There is no heap, so the context owns storage for every state it can be in and hands
 * that storage out when the state changes. The struct is defined here, not hidden, so Game
 * can hold contexts by value. The header is still private to the library (src/, not
 * include/). */

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "regular_frame.h"
#include "spare_frame.h"
#include "strike_frame.h"
#include "tenth_frame.h"

struct FrameStateFactory; /* defined in frame_context.c: callers never need its contents */

typedef struct FrameContext {
    Frame *current_state;
    const struct FrameStateFactory *factory;
    RegularFrame regular;
    SpareFrame spare;
    StrikeFrame strike;
    TenthSpareFrame tenth_spare;
    TenthStrikeFrame tenth_strike;
} FrameContext;

/* A frame in 1 to 9, or the last frame. Both start as a RegularFrame. */
void FrameContext_Init(FrameContext *self);
void FrameContext_InitTenth(FrameContext *self);

/* The next state, from this context's family. Each builds the state in the context's own
 * storage and returns it, ready for FrameContext_SetState. */
Frame *FrameContext_NewStrikeFrame(FrameContext *self);
Frame *FrameContext_NewSpareFrame(FrameContext *self, const Frame *replaced,
                                  uint8_t completing_pins);

void FrameContext_SetState(FrameContext *self, Frame *frame_state);
RollResult FrameContext_Roll(FrameContext *self, uint8_t pins);
uint16_t FrameContext_Score(const FrameContext *self);
bool FrameContext_IsComplete(const FrameContext *self);
uint8_t FrameContext_PinsStanding(const FrameContext *self);

#endif /* FRAME_CONTEXT_H */
