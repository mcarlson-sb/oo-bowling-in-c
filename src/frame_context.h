#ifndef FRAME_CONTEXT_H
#define FRAME_CONTEXT_H

/* The State-pattern context: one per frame. Holds the frame's current state and forwards
 * each call to it.
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

typedef struct FrameContext {
    Frame *current_state;
    RegularFrame regular;
    SpareFrame spare;
    StrikeFrame strike;
    TenthFrame tenth;
} FrameContext;

/* Frames 1 to 9 start as a RegularFrame; the last frame starts, and stays, a TenthFrame. */
void FrameContext_Init(FrameContext *self);
void FrameContext_InitTenth(FrameContext *self);

/* Factories for the next state. Each builds the state in this context's own storage and
 * returns it, ready for FrameContext_SetState. */
Frame *FrameContext_NewStrikeFrame(FrameContext *self);
Frame *FrameContext_NewSpareFrame(FrameContext *self, const uint8_t *rolls, uint8_t roll_count);

void FrameContext_SetState(FrameContext *self, Frame *frame_state);
RollResult FrameContext_Roll(FrameContext *self, uint8_t pins);
uint16_t FrameContext_Score(const FrameContext *self);
bool FrameContext_IsOpen(const FrameContext *self);
uint8_t FrameContext_PinsStanding(const FrameContext *self);

#endif /* FRAME_CONTEXT_H */
