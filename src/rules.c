#include "rules.h"

/* The most balls the last frame takes, its fill balls included. */
static uint8_t ScorerRules_LastFrameBalls(const ScorerRules *rules)
{
    uint8_t most = rules->balls_per_frame;
    for (uint8_t ball = 1U; ball <= rules->balls_per_frame; ball++) {
        const uint8_t with_fill = (uint8_t)(ball + rules->bonus_balls_by_clearing_ball[ball - 1U]);
        most = (with_fill > most) ? with_fill : most;
    }
    return most;
}

unsigned ScorerRules_LongestGame(const ScorerRules *rules)
{
    return (((unsigned)rules->frames - 1U) * rules->balls_per_frame) +
           ScorerRules_LastFrameBalls(rules);
}

static bool ScorerRules_HasFramesItHolds(const ScorerRules *rules)
{
    return (rules->frames != 0U) && (rules->frames <= SCORER_MAX_FRAMES);
}

static bool ScorerRules_HasBallsItHolds(const ScorerRules *rules)
{
    return (rules->balls_per_frame != 0U) &&
           (rules->balls_per_frame <= SCORER_MAX_BALLS_PER_FRAME);
}

static bool ScorerRules_BonusesStopAtTheFramesBalls(const ScorerRules *rules)
{
    for (uint8_t ball = rules->balls_per_frame; ball < SCORER_MAX_BALLS_PER_FRAME; ball++) {
        if (rules->bonus_balls_by_clearing_ball[ball] != 0U) {
            return false;
        }
    }
    return true;
}

/* Some pins, few enough to score, and a clear that a ball must earn. */
static bool ScorerRules_HasARackItScores(const ScorerRules *rules)
{
    return (rules->pins_per_rack != 0U) && (rules->pins_per_rack <= SCORER_MAX_PINS_PER_RACK) &&
           (rules->pins_standing_that_count_as_a_clear < rules->pins_per_rack);
}

/* The longest game only once the frames and balls fit: it counts both, and reads a bonus for
 * each ball of the frame. */
bool ScorerRules_AreValid(const ScorerRules *rules)
{
    return ScorerRules_HasFramesItHolds(rules) && ScorerRules_HasBallsItHolds(rules) &&
           ScorerRules_BonusesStopAtTheFramesBalls(rules) && ScorerRules_HasARackItScores(rules) &&
           (ScorerRules_LongestGame(rules) <= SCORER_MAX_BALLS);
}
