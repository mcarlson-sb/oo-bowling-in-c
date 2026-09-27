#ifndef FRAME_H
#define FRAME_H

/* The abstract base of the frame states. Each state embeds Frame as its first member, so a
 * pointer to the state is also a pointer to its Frame. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "roll_list.h"

/* `pins` means something only when !consumed. */
typedef struct {
    bool consumed;
    Pins pins;
} RollResult;

/* No compound literals: the C++ tests include this header. */
static inline RollResult RollResult_Consumed(void)
{
    const RollResult result = { true, 0U };
    return result;
}

static inline RollResult RollResult_Passed(Pins pins)
{
    const RollResult result = { false, pins };
    return result;
}

struct FrameContext;
typedef struct Frame Frame;

#define FRAME_ALL_PINS 10U

/* The state interface: every state keeps this contract, so any can stand in for a Frame. */
typedef struct {
    /* Given an incomplete frame, no more pins than pins_standing() allows, and the frame's own
     * context (passed in, so no state points back to it). Records the roll as a roll or bonus
     * roll, and returns Consumed, or Passed(pins) for the next frame too. Changes state only
     * through `context`. */
    RollResult (*roll)(Frame *self, struct FrameContext *context, Pins pins);

    /* For the next roll, if this is the latest frame. No side effects. */
    Pins (*pins_standing)(const Frame *self);
} FrameVtable;

struct Frame {
    const FrameVtable *vtable;
    RollList rolls;
    RollList bonus_rolls; /* the tenth frame's fill balls too */
    bool complete;
};

void Frame_Init(Frame *self, const FrameVtable *vtable);

/* A complete frame passes the roll on without calling its state. */
RollResult Frame_Roll(Frame *self, struct FrameContext *context, Pins pins);
Pins Frame_PinsStanding(const Frame *self);

Pins Frame_AllPinsStanding(const Frame *self);

/* For the states, which never write the fields directly. */
void Frame_AddRoll(Frame *self, Pins pins);
void Frame_AddBonusRoll(Frame *self, Pins pins);
void Frame_Complete(Frame *self);

/* A strike, in either family: born with one roll, of all the pins. */
Frame *Frame_InitStrike(Frame *self, const FrameVtable *vtable);

/* A spare, in either family: born with the replaced frame's rolls, plus the one that
 * completed it. */
Frame *Frame_InitSpare(Frame *self, const FrameVtable *vtable, const Frame *replaced,
                       Pins completing_pins);

bool Frame_IsComplete(const Frame *self);

/* Its own rolls, without bonus rolls. */
Pins Frame_PinsKnockedDown(const Frame *self);

bool Frame_NextIsFirstRoll(const Frame *self);
bool Frame_NextIsSecondBonusRoll(const Frame *self);

/* Only once there is one. */
Pins Frame_FirstBonusRoll(const Frame *self);

bool Frame_HasAllRolls(const Frame *self);
bool Frame_HasAllBonusRolls(const Frame *self);

/* The same for every state, so not virtual. 0 until the frame is complete. */
Score Frame_Score(const Frame *self);

#endif /* FRAME_H */
