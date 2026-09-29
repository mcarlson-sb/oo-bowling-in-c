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

/* Storage for the largest variant, candlepin (see scorer.c's static asserts). */
#define SCORER_MAX_BALLS 30U
#define SCORER_MAX_FRAMES 10U

/* An operation tells each frame at most once. */
#define SCORER_MAX_EVENTS SCORER_MAX_FRAMES

typedef enum {
    SCORER_TEN_PIN = 0,
    SCORER_CANDLEPIN
} ScorerVariant;

/* How a ball counts, given the pins standing and the pins that fell: a closed set, chosen by
 * data, where kay-oo took a caller's function. */
typedef enum {
    SCORER_COUNT_PINS_DOWN = 0,
    /* A ball that leaves one pin standing from a full rack counts as clearing it. */
    SCORER_COUNT_NO_TAP
} CountRule;

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

/* One frame: its score, and whether that score is known yet. */
typedef struct {
    Score score; /* 0 until complete */
    bool complete;
} ScorerFrame;

typedef struct {
    ScorerVariant variant;
    CountRule rule;
    Pins balls[SCORER_MAX_BALLS]; /* as they fell */
    uint8_t ball_count;
} Scorer;

/* Counting the pins that fell. */
void Scorer_Init(Scorer *self, ScorerVariant variant);

void Scorer_InitWithRule(Scorer *self, ScorerVariant variant, CountRule rule);

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events);

/* Applies the edit (see RollEdit) and replays every ball; reports every frame again, the final
 * state only, with complete = false for a frame the edit reopened. Rejected, changing nothing,
 * with the status of the first ball it makes impossible, or with GAME_ERR_INVALID_EDIT,
 * GAME_ERR_NO_SUCH_ROLL or GAME_ERR_TOO_MANY_ROLLS. */
GameStatus Scorer_Edit(Scorer *self, const RollEdit *edit, FrameEvents *events);

/* The total of the complete frames. */
Score Scorer_Score(const Scorer *self);

/* The pins standing for the next ball. */
Pins Scorer_PinsStanding(const Scorer *self);

/* True once the last frame, and its fill balls, are done. */
bool Scorer_IsOver(const Scorer *self);

/* Frames started so far: a frame starts with its first ball. */
uint8_t Scorer_FrameCount(const Scorer *self);

/* Frame `index` (the first is 0), which must have started. */
ScorerFrame Scorer_Frame(const Scorer *self, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* SCORER_H */
