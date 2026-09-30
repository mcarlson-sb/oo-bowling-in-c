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

#endif /* BOWLING_TYPES_H */
