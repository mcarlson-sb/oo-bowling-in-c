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

#define FRAME_MAX_ROLLS 2U

/* What roll() did with the pins: kept them (consumed), or passed them on to the next
 * frame. `pins` is meaningful only when !consumed. */
typedef struct {
    bool consumed;
    uint8_t pins;
} RollResult;

/* Plain initializers, not C99 compound literals, so the header is also valid C++ for the
 * white-box tests. */
static inline RollResult RollResult_Consumed(void)
{
    const RollResult result = { true, 0U };
    return result;
}

static inline RollResult RollResult_Passed(uint8_t pins)
{
    const RollResult result = { false, pins };
    return result;
}

struct FrameContext;
typedef struct Frame Frame;

/* The most bonus rolls any frame earns (a strike's two). */
#define FRAME_MAX_BONUS_ROLLS 2U

#define FRAME_ALL_PINS 10U

typedef struct {
    /* The context passes itself in, so a state can switch the context to its next state
     * without every frame storing a pointer back to it. */
    RollResult (*roll)(Frame *self, struct FrameContext *context, uint8_t pins);
    /* Pins standing for the next roll, if this is the game's latest frame:
     * FRAME_ALL_PINS unless the next roll is this frame's own, on a partly cleared rack. */
    uint8_t (*pins_standing)(const Frame *self);
} FrameVtable;

struct Frame {
    const FrameVtable *vtable;
    uint8_t rolls[FRAME_MAX_ROLLS];
    uint8_t roll_count;
    uint8_t bonus_rolls[FRAME_MAX_BONUS_ROLLS];
    uint8_t bonus_count; /* 0 for a RegularFrame, which earns no bonus */
    bool complete; /* all rolls and bonus rolls are in, so the score is final */
};

/* Constructor for the base part; called by each derived class's constructor. */
void Frame_Init(Frame *self, const FrameVtable *vtable);

/* Virtual: each state rolls differently. A complete frame passes the roll on
 * without calling its state. */
RollResult Frame_Roll(Frame *self, struct FrameContext *context, uint8_t pins);
uint8_t Frame_PinsStanding(const Frame *self);

/* The pins_standing for any state whose next roll always starts on a full rack. */
uint8_t Frame_AllPinsStanding(const Frame *self);

/* For derived classes only: the base class owns its fields' rules, so states change them
 * through these, never by writing the fields directly. */
void Frame_AddRoll(Frame *self, uint8_t pins);
void Frame_AddBonusRoll(Frame *self, uint8_t pins);
void Frame_Complete(Frame *self);

/* Copies another frame's rolls into this one, for a state built from the one it replaces. */
void Frame_CopyRolls(Frame *self, const Frame *from);

/* Pins knocked down by this frame's own rolls, without bonus rolls. */
uint8_t Frame_PinsKnockedDown(const Frame *self);

/* Not virtual: every state scores the same way, as its rolls plus its bonus rolls, and 0
 * until the frame is complete. */
uint16_t Frame_Score(const Frame *self);

#endif /* FRAME_H */
