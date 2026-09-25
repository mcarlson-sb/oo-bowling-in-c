#ifndef FRAME_CONTEXT_H
#define FRAME_CONTEXT_H

/* The State-pattern context (Bowling-OO frame-context.ts). Holds the frame's current
 * state and delegates roll() and score() to it.
 *
 * TypeScript creates each new state with `new`. With no heap, the context owns the storage
 * for its states and hands it out when a state changes. The struct is defined here, not
 * hidden, so Game can hold contexts by value. The header is still private to the library
 * (src/, not include/). */

#include <stdint.h>

#include "frame.h"
#include "regular_frame.h"
#include "spare_frame.h"

typedef struct FrameContext {
    Frame *current_state;
    RegularFrame regular;
    SpareFrame spare;
} FrameContext;

void FrameContext_Init(FrameContext *self);

/* Stand-in for `new SpareFrame(context, rolls)`: builds the state in this context's own
 * storage and returns it, ready for FrameContext_SetState. */
Frame *FrameContext_NewSpareFrame(FrameContext *self, const uint8_t *rolls, uint8_t roll_count);

void FrameContext_SetState(FrameContext *self, Frame *frame_state);
int16_t FrameContext_Roll(FrameContext *self, uint8_t pins);
uint16_t FrameContext_Score(const FrameContext *self);

#endif /* FRAME_CONTEXT_H */
