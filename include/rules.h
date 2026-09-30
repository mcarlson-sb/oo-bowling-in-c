#ifndef RULES_H
#define RULES_H

/* The game's shared vocabulary: its limits, a variant's rules and whether they can be played,
 * and the news of one frame. The scorer plays by these, and the protocol and every kind speak
 * them, without depending on the scorer itself. */

#include <stdbool.h>
#include <stdint.h>

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

/* Frames count from 1, as a scorer counts them; an index into a game's frames counts from 0. */
static inline FrameNumber FrameNumber_OfIndex(uint8_t index)
{
    return (FrameNumber)(index + 1U);
}

static inline uint8_t FrameNumber_ToIndex(FrameNumber number)
{
    return (uint8_t)(number - 1U);
}

/* Rules the scorer can hold and play. */
bool ScorerRules_AreValid(const ScorerRules *rules);

/* The most balls a game by these rules can take, fill balls included. Only for valid rules. */
unsigned ScorerRules_LongestGame(const ScorerRules *rules);

#ifdef __cplusplus
}
#endif

#endif /* RULES_H */
