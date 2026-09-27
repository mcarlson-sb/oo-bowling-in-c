#ifndef BOWLING_TYPES_H
#define BOWLING_TYPES_H

/* The domain's two quantities, named so that signatures say what they carry.
 *
 * These are C typedefs, so they document intent but the compiler still treats them as the
 * underlying integers: passing a roll count where Pins is expected compiles without a
 * warning. A one-field struct would catch that, but it would need a helper function for
 * every piece of arithmetic. Game_Roll checks every pin count at the API boundary, so the
 * typedef is the chosen trade-off. */

#include <stdint.h>

/* Pins knocked down or standing: 0 to 10 for a roll, up to 20 for a frame's rolls. */
typedef uint8_t Pins;

/* Points: a frame scores at most 30, and a game at most 300. */
typedef uint16_t Score;

/* A roll's number in its game, counting from 1, the way a scorer counts: not an array index,
 * which counts from 0. */
typedef uint8_t RollNumber;

/* A frame's number in its game, 1 to 10. */
typedef uint8_t FrameNumber;

#endif /* BOWLING_TYPES_H */
