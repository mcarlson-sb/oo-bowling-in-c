#ifndef SCORER_H
#define SCORER_H

/* The scorer core: one generic scorer for every variant of the game, which plays whatever rules
 * it is started with. It is pure: an operation changes only the scorer it is given, and reports
 * the frames it changed in a buffer the caller supplies.
 *
 * A Scorer is a plain value, its rules and the balls it has taken, so it can be copied, and a
 * copy is a separate game. Treat its fields as private: use the functions. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_status.h"
#include "bowling_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCORER_MAX_BALLS 30U
#define SCORER_MAX_FRAMES 10U
#define SCORER_MAX_BALLS_PER_FRAME 3U
#define SCORER_MAX_PINS_PER_RACK 20U

#define SCORER_MAX_EVENTS SCORER_MAX_FRAMES

typedef enum {
    SCORER_CLEARED_BY_FIRST_BALL,
    SCORER_CLEARED_BY_SECOND_BALL,
    SCORER_CLEARED_BY_THIRD_BALL
} ScorerClearingBall;

/* A variant's rules, as data: the game is whatever these say. */
typedef struct {
    uint8_t frames;
    uint8_t balls_per_frame;
    Pins pins_per_rack;
    uint8_t bonus_balls_by_clearing_ball[SCORER_MAX_BALLS_PER_FRAME];
    /* Off a full rack, a ball that leaves this many standing or fewer counts as clearing it:
     * 0 counts the pins that fell, 1 is nine-pin no-tap. */
    Pins pins_standing_that_count_as_a_clear;
} ScorerRules;

typedef struct {
    FrameNumber frame_number;
    Score frame_score;
    bool frame_complete;
} FrameEvent;

typedef struct {
    FrameEvent events[SCORER_MAX_EVENTS];
    uint8_t count;
} FrameEvents;

typedef struct {
    Score score;
    bool complete;
} ScorerFrame;

typedef struct {
    ScorerRules rules;
    uint8_t max_balls;
    Pins balls[SCORER_MAX_BALLS];
    uint8_t ball_count;
} Scorer;

/* A new game by these rules. Rejected, changing nothing, with GAME_ERR_INVALID_RULES. */
GameStatus Scorer_Start(Scorer *self, const ScorerRules *rules);

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events);

/* Applies the edit (see RollEdit) and replays every ball; reports every frame again, the final
 * state only, with complete = false for a frame the edit reopened. Rejected, changing nothing,
 * with the status of the first ball it makes impossible, or with GAME_ERR_INVALID_EDIT,
 * GAME_ERR_NO_SUCH_ROLL or GAME_ERR_TOO_MANY_ROLLS. */
GameStatus Scorer_Edit(Scorer *self, const RollEdit *edit, FrameEvents *events);

/* The total of the complete frames. */
Score Scorer_Score(const Scorer *self);

Pins Scorer_PinsStanding(const Scorer *self);

/* True once the last frame, and its fill balls, are done. */
bool Scorer_IsOver(const Scorer *self);

uint8_t Scorer_BallCount(const Scorer *self);

uint8_t Scorer_FramesStarted(const Scorer *self);

ScorerFrame Scorer_Frame(const Scorer *self, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* SCORER_H */
