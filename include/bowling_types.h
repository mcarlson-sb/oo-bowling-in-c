#ifndef BOWLING_TYPES_H
#define BOWLING_TYPES_H

/* Plain typedefs, not one-field structs: the compiler won't catch a mix-up, but arithmetic
 * stays arithmetic, and Game_Roll checks every pin count it is given. */

#include <stdint.h>

/* 0 to 10 for a roll, up to 20 for a frame's rolls. */
typedef uint8_t Pins;

/* At most 30 for a frame, 300 for a game. */
typedef uint16_t Score;

/* Counts from 1, the way a scorer counts; not an index. */
typedef uint8_t RollNumber;

/* 1 to 10. */
typedef uint8_t FrameNumber;

/* Replaces rolls_removed rolls, from roll number first_roll (the first is 1), with the new_count
 * rolls in new_pins: a replacement, an insertion (none removed) or a deletion (none new). */
typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    const Pins *new_pins; /* NULL only when new_count is 0 */
    uint8_t new_count;
} RollEdit;

#endif /* BOWLING_TYPES_H */
