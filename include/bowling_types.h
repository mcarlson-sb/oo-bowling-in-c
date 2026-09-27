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

#endif /* BOWLING_TYPES_H */
