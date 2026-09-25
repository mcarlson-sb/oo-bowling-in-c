#ifndef FRAME_H
#define FRAME_H

/* Abstract base class for a frame state (Bowling-OO frame.ts).
 *
 * C has no classes, so the pattern is spelled out:
 *   - The "class" is a struct whose first member is a pointer to a const vtable.
 *   - "abstract" methods are vtable entries; there is no Frame vtable of its own, so a
 *     plain Frame cannot be used on its own.
 *   - A derived class embeds Frame as its FIRST member, so a pointer to the derived struct
 *     is also a valid pointer to its Frame, and the reverse cast is safe inside the
 *     derived class's own methods.
 *
 * Private to the library: lives in src/, not include/. */

#include <stdbool.h>
#include <stdint.h>

#define FRAME_MAX_ROLLS 2U

/* Returned by roll() when the frame kept the pins. Stands in for TypeScript's `null`;
 * any other value is the pin count, passed on to the next frame. */
#define FRAME_ROLL_CONSUMED ((int16_t)-1)

struct FrameContext;
typedef struct Frame Frame;

typedef struct {
    int16_t (*roll)(Frame *self, uint8_t pins);
    uint16_t (*score)(const Frame *self);
} FrameVtable;

struct Frame {
    const FrameVtable *vtable;
    struct FrameContext *context;
    uint8_t rolls[FRAME_MAX_ROLLS];
    uint8_t roll_count;
    bool open;
};

/* Constructor for the base part; called by each derived class's constructor. */
void Frame_Init(Frame *self, const FrameVtable *vtable, struct FrameContext *context);

/* Virtual dispatch. */
int16_t Frame_Roll(Frame *self, uint8_t pins);
uint16_t Frame_Score(const Frame *self);

#endif /* FRAME_H */
