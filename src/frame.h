#ifndef FRAME_H
#define FRAME_H

/* Abstract base class for a frame state.
 *
 * C has no classes, so the pattern is spelled out:
 *   - The "class" is a struct whose first member is a pointer to a const vtable.
 *   - "abstract" methods are vtable entries. Frame has no vtable of its own; only the
 *     derived classes define one, so every usable Frame is one of them.
 *   - A derived class embeds Frame as its FIRST member, so a pointer to the derived struct
 *     is also a valid pointer to its Frame.
 *
 * Private to the library: lives in src/, not include/. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "roll_list.h"

/* What roll() did with the pins: kept them (consumed), or passed them on to the next
 * frame. `pins` is meaningful only when !consumed. */
typedef struct {
    bool consumed;
    Pins pins;
} RollResult;

/* Plain initializers, not C99 compound literals, so the header is also valid C++ for the
 * white-box tests. */
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

/* The state interface. Every state honors the same contract, so any of them can stand in
 * for a Frame (Liskov substitution), and callers never need to know which one they have. */
typedef struct {
    /* Takes one roll.
     *
     * The caller guarantees:
     *   - the frame is incomplete (Frame_Roll handles complete frames itself);
     *   - `pins` is no more than pins_standing() allowed (Game_Roll has checked it);
     *   - `context` is this frame's own context, never NULL.
     *
     * The state guarantees:
     *   - it records the roll as one of its own rolls or bonus rolls, never more than a
     *     RollList holds;
     *   - it returns RollResult_Consumed() if it keeps the roll, or RollResult_Passed(pins),
     *     with the same pins, if the next frame should also get it;
     *   - it changes state only through `context`. Only RegularFrame does; the others
     *     ignore `context`.
     *
     * The context passes itself in so that no frame stores a pointer back to it. */
    RollResult (*roll)(Frame *self, struct FrameContext *context, Pins pins);

    /* Pins standing for the next roll, if this is the game's latest frame: from 0 to
     * FRAME_ALL_PINS. FRAME_ALL_PINS unless the next roll is this frame's own, on a partly
     * cleared rack. Has no side effects. */
    Pins (*pins_standing)(const Frame *self);
} FrameVtable;

struct Frame {
    const FrameVtable *vtable;
    RollList rolls;       /* the frame's own rolls: at most two */
    RollList bonus_rolls; /* strike: two, spare: one, regular frame: none */
    bool complete;        /* all rolls and bonus rolls are in, so the score is final */
};

/* Constructor for the base part; called by each derived class's constructor. */
void Frame_Init(Frame *self, const FrameVtable *vtable);

/* Virtual: each state rolls differently, under the contract on FrameVtable.roll above. A
 * complete frame passes the roll on without calling its state. */
RollResult Frame_Roll(Frame *self, struct FrameContext *context, Pins pins);
Pins Frame_PinsStanding(const Frame *self);

/* The pins_standing for any state whose next roll always starts on a full rack. */
Pins Frame_AllPinsStanding(const Frame *self);

/* For derived classes only: the base class owns its fields' rules, so states change them
 * through these, never by writing the fields directly. */
void Frame_AddRoll(Frame *self, Pins pins);
void Frame_AddBonusRoll(Frame *self, Pins pins);
void Frame_Complete(Frame *self);

/* Copies another frame's rolls into this one, for a state built from the one it replaces. */
void Frame_CopyRolls(Frame *self, const Frame *from);

/* Pins knocked down by this frame's own rolls, without bonus rolls. */
Pins Frame_PinsKnockedDown(const Frame *self);

/* Which roll comes next: the frame's first, or its second bonus roll. */
bool Frame_IsFirstRoll(const Frame *self);
bool Frame_IsSecondBonusRoll(const Frame *self);

/* The pins knocked down by the frame's first bonus roll. Only valid once there is one. */
Pins Frame_FirstBonusRoll(const Frame *self);

/* Whether the frame has all the rolls, or all the bonus rolls, it can hold. */
bool Frame_HasAllRolls(const Frame *self);
bool Frame_HasAllBonusRolls(const Frame *self);

/* Not virtual: every state scores the same way, as its rolls plus its bonus rolls, and 0
 * until the frame is complete. */
Score Frame_Score(const Frame *self);

#endif /* FRAME_H */
