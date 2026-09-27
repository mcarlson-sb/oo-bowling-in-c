#ifndef FRAME_CONTEXT_H
#define FRAME_CONTEXT_H

/* The State pattern's context, one per frame. Its family of states (an Abstract Factory)
 * decides what a strike or spare becomes: frames 1 to 9 pass their bonus rolls on, and the
 * tenth keeps its fill balls. With no heap, it holds storage for every state it can be in;
 * the struct is visible so Game can hold contexts by value. */

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "regular_frame.h"
#include "spare_frame.h"
#include "strike_frame.h"
#include "tenth_frame.h"

struct FrameStateFactory;

typedef struct FrameContext {
    Frame *current_state;
    const struct FrameStateFactory *factory;
    RegularFrame regular;
    SpareFrame spare;
    StrikeFrame strike;
    TenthSpareFrame tenth_spare;
    TenthStrikeFrame tenth_strike;
} FrameContext;

void FrameContext_Init(FrameContext *self);
void FrameContext_InitTenth(FrameContext *self);

/* Game's side. A state's side is in frame_transition.h. */
RollResult FrameContext_Roll(FrameContext *self, Pins pins);
Score FrameContext_Score(const FrameContext *self);
bool FrameContext_IsComplete(const FrameContext *self);
Pins FrameContext_PinsStanding(const FrameContext *self);

#endif /* FRAME_CONTEXT_H */
