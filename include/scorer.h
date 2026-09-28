#ifndef SCORER_H
#define SCORER_H

/* The scorer core: one generic scorer for every variant of the game, driven by a table of
 * rules, with no callbacks and no function pointers. It is pure: an operation changes only the
 * scorer it is given, and reports the frames it changed in a buffer the caller supplies.
 *
 * A Scorer is a plain value, its variant and the balls it has taken, so it can be copied, and a
 * copy is a separate game. Treat its fields as private: use the functions. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_status.h"
#include "bowling_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Storage for the largest variant. */
#define SCORER_MAX_BALLS 30U
#define SCORER_MAX_FRAMES 10U

/* An operation tells each frame at most once. */
#define SCORER_MAX_EVENTS SCORER_MAX_FRAMES

typedef enum {
    SCORER_TEN_PIN = 0
} ScorerVariant;

/* A frame's number (1 to 10), its score, and whether it is complete. */
typedef struct {
    FrameNumber frame_number;
    Score frame_score;
    bool frame_complete;
} FrameEvent;

/* The frames one operation changed, oldest first. */
typedef struct {
    FrameEvent events[SCORER_MAX_EVENTS];
    uint8_t count;
} FrameEvents;

typedef struct {
    ScorerVariant variant;
    Pins balls[SCORER_MAX_BALLS]; /* as they fell */
    uint8_t ball_count;
} Scorer;

void Scorer_Init(Scorer *self, ScorerVariant variant);

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events);

/* The total of the complete frames. */
Score Scorer_Score(const Scorer *self);

#ifdef __cplusplus
}
#endif

#endif /* SCORER_H */
