#ifndef BOWLING_TYPES_H
#define BOWLING_TYPES_H

/* Plain typedefs, not one-field structs: the compiler won't catch a mix-up, but arithmetic
 * stays arithmetic. */

#include <stdint.h>

typedef uint8_t Pins;

typedef uint16_t Score;

/* Counts from 1, the way a scorer counts; not an index. */
typedef uint8_t RollNumber;

typedef uint8_t FrameNumber;

/* Replaces rolls_removed rolls, from roll number first_roll (the first is 1), with the new_count
 * rolls in new_pins: a replacement, an insertion (none removed) or a deletion (none new). */
typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    const Pins *new_pins;
    uint8_t new_count;
} RollEdit;

#endif /* BOWLING_TYPES_H */
