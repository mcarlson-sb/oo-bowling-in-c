#include "scorer.h"

#include <assert.h>
#include <stddef.h>

#define SCORER_MAX_BALLS_PER_FRAME 3U

typedef enum {
    CLEARED_BY_STRIKE,
    CLEARED_BY_SPARE,
    CLEARED_BY_TEN_BOX
} ClearingBall;

typedef struct {
    uint8_t frames;
    uint8_t balls_per_frame;
    Pins pins_per_rack;
    uint8_t max_balls_per_game;
    uint8_t bonus_balls_by_clearing_ball[SCORER_MAX_BALLS_PER_FRAME];
} VariantRules;

/* The last frame takes fill balls up to the same SCORER_MAX_BALLS_PER_FRAME, in any variant. */
#define LONGEST_GAME(frames, balls_per_frame) \
    ((((frames) - 1U) * (balls_per_frame)) + SCORER_MAX_BALLS_PER_FRAME)
#define TEN_PIN_MAX_BALLS LONGEST_GAME(10U, 2U)
#define CANDLEPIN_MAX_BALLS LONGEST_GAME(10U, 3U)

_Static_assert(TEN_PIN_MAX_BALLS <= SCORER_MAX_BALLS, "ten-pin must fit the scorer's storage");
_Static_assert(CANDLEPIN_MAX_BALLS <= SCORER_MAX_BALLS, "candlepin must fit the scorer's storage");

static const VariantRules k_variant_rules[] = {
    [SCORER_TEN_PIN] = { .frames = 10U, .balls_per_frame = 2U, .pins_per_rack = 10U,
                         .max_balls_per_game = TEN_PIN_MAX_BALLS,
                         .bonus_balls_by_clearing_ball = { [CLEARED_BY_STRIKE] = 2U,
                                                           [CLEARED_BY_SPARE] = 1U } },
    [SCORER_CANDLEPIN] = { .frames = 10U, .balls_per_frame = 3U, .pins_per_rack = 10U,
                           .max_balls_per_game = CANDLEPIN_MAX_BALLS,
                           .bonus_balls_by_clearing_ball = { [CLEARED_BY_STRIKE] = 2U,
                                                             [CLEARED_BY_SPARE] = 1U,
                                                             [CLEARED_BY_TEN_BOX] = 0U } },
};

typedef struct {
    uint8_t first_ball;
    uint8_t own_balls;
    uint8_t bonus_balls;
    bool closed;
} FrameShape;

typedef enum {
    LANE_TAKING_FRAMES,
    LANE_TAKING_FILL_BALLS, /* the last frame's bonus balls: they start no frame */
    LANE_OVER
} LanePhase;

typedef struct {
    FrameShape frames[SCORER_MAX_FRAMES];
    uint8_t frames_started;
    Pins counted_pins[SCORER_MAX_BALLS];
    Pins standing;
    LanePhase phase;
    uint8_t fill_balls_left;
} Lane;

static const VariantRules *Scorer_Rules(const Scorer *self)
{
    return &k_variant_rules[self->variant];
}

static bool Lane_NeedsNewFrame(const Lane *lane)
{
    return (lane->frames_started == 0U) || lane->frames[lane->frames_started - 1U].closed;
}

static FrameShape *Lane_StartFrame(Lane *lane, uint8_t ball_index)
{
    FrameShape *frame = &lane->frames[lane->frames_started];
    frame->first_ball = ball_index;
    frame->own_balls = 0U;
    frame->bonus_balls = 0U;
    frame->closed = false;
    lane->frames_started++;
    return frame;
}

static FrameShape *Lane_FrameTakingBall(Lane *lane, uint8_t ball_index)
{
    if (Lane_NeedsNewFrame(lane)) {
        return Lane_StartFrame(lane, ball_index);
    }
    return &lane->frames[lane->frames_started - 1U];
}

static ClearingBall FrameShape_ClearingBall(const FrameShape *frame)
{
    return (ClearingBall)(frame->own_balls - 1U);
}

static void FrameShape_CloseClearingTheRack(FrameShape *frame, const VariantRules *rules)
{
    frame->closed = true;
    frame->bonus_balls = rules->bonus_balls_by_clearing_ball[FrameShape_ClearingBall(frame)];
}

static bool Lane_IsLastFrame(const Lane *lane, const VariantRules *rules)
{
    return lane->frames_started == rules->frames;
}

static void Lane_ResetRack(Lane *lane, const VariantRules *rules)
{
    lane->standing = rules->pins_per_rack;
}

static bool Lane_IsRackCleared(const Lane *lane)
{
    return lane->standing == 0U;
}

static void Lane_KnockDown(Lane *lane, Pins pins)
{
    lane->standing = (Pins)(lane->standing - pins);
}

static void Lane_StartFillBalls(Lane *lane, uint8_t fill_balls)
{
    lane->fill_balls_left = fill_balls;
    lane->phase = (fill_balls > 0U) ? LANE_TAKING_FILL_BALLS : LANE_OVER;
}

static void Lane_UseFillBall(Lane *lane)
{
    lane->fill_balls_left--;
    if (lane->fill_balls_left == 0U) {
        lane->phase = LANE_OVER;
    }
}

static void Lane_AfterFrameCloses(Lane *lane, const VariantRules *rules, const FrameShape *frame)
{
    Lane_ResetRack(lane, rules);
    if (Lane_IsLastFrame(lane, rules)) {
        Lane_StartFillBalls(lane, frame->bonus_balls);
    }
}

static void FrameShape_TakeBall(FrameShape *frame)
{
    frame->own_balls++;
}

static void FrameShape_CloseIfDone(FrameShape *frame, const VariantRules *rules, bool cleared)
{
    if (cleared) {
        FrameShape_CloseClearingTheRack(frame, rules);
    } else if (frame->own_balls == rules->balls_per_frame) {
        frame->closed = true;
    }
}

static void Lane_ThrowInFrame(Lane *lane, const VariantRules *rules, uint8_t ball_index, Pins pins)
{
    FrameShape *frame = Lane_FrameTakingBall(lane, ball_index);
    FrameShape_TakeBall(frame);
    Lane_KnockDown(lane, pins);
    FrameShape_CloseIfDone(frame, rules, Lane_IsRackCleared(lane));
    if (frame->closed) {
        Lane_AfterFrameCloses(lane, rules, frame);
    }
}

static void Lane_ThrowFillBall(Lane *lane, const VariantRules *rules, Pins pins)
{
    Lane_KnockDown(lane, pins);
    if (Lane_IsRackCleared(lane)) {
        Lane_ResetRack(lane, rules);
    }
    Lane_UseFillBall(lane);
}

static void Lane_Throw(Lane *lane, const VariantRules *rules, uint8_t ball_index, Pins pins)
{
    switch (lane->phase) {
    case LANE_TAKING_FRAMES:
        Lane_ThrowInFrame(lane, rules, ball_index, pins);
        break;
    case LANE_TAKING_FILL_BALLS:
        Lane_ThrowFillBall(lane, rules, pins);
        break;
    case LANE_OVER:
    default:
        assert(!"Lane_CheckBall refuses a ball once the lane is over");
        break;
    }
}

static bool NoTap_LeavesOnePinOfAFullRack(Pins standing, Pins pins, Pins full_rack)
{
    return (standing == full_rack) && ((Pins)(pins + 1U) == standing);
}

static Pins Scorer_CountPins(const Scorer *self, Pins standing, Pins pins)
{
    switch (self->rule) {
    case SCORER_COUNT_NO_TAP:
        return NoTap_LeavesOnePinOfAFullRack(standing, pins, Scorer_Rules(self)->pins_per_rack)
                   ? standing
                   : pins;
    case SCORER_COUNT_PINS_DOWN:
    default:
        return pins;
    }
}

static Lane Scorer_Lane(const Scorer *self)
{
    const VariantRules *rules = Scorer_Rules(self);
    Lane lane;
    lane.frames_started = 0U;
    lane.standing = rules->pins_per_rack;
    lane.phase = LANE_TAKING_FRAMES;
    lane.fill_balls_left = 0U;
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        lane.counted_pins[i] = Scorer_CountPins(self, lane.standing, self->balls[i]);
        Lane_Throw(&lane, rules, i, lane.counted_pins[i]);
    }
    return lane;
}

static uint8_t FrameShape_EndOfScoredBalls(const FrameShape *frame)
{
    return (uint8_t)(frame->first_ball + frame->own_balls + frame->bonus_balls);
}

static bool FrameShape_IsComplete(const FrameShape *frame, uint8_t ball_count)
{
    return frame->closed && (ball_count >= FrameShape_EndOfScoredBalls(frame));
}

static Score Lane_FrameScore(const Lane *lane, const FrameShape *frame)
{
    Score score = 0U;
    for (uint8_t i = frame->first_ball; i < FrameShape_EndOfScoredBalls(frame); i++) {
        score = (Score)(score + lane->counted_pins[i]);
    }
    return score;
}

void Scorer_Init(Scorer *self, ScorerVariant variant)
{
    Scorer_InitWithRule(self, variant, SCORER_COUNT_PINS_DOWN);
}

void Scorer_InitWithRule(Scorer *self, ScorerVariant variant, CountRule rule)
{
    self->variant = variant;
    self->rule = rule;
    self->ball_count = 0U;
}

static uint8_t Lane_CountCompleteFrames(const Lane *lane, uint8_t ball_count)
{
    uint8_t complete = 0U;
    while ((complete < lane->frames_started) &&
           FrameShape_IsComplete(&lane->frames[complete], ball_count)) {
        complete++;
    }
    return complete;
}

static uint8_t Scorer_CountCompleteFrames(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return Lane_CountCompleteFrames(&lane, self->ball_count);
}

static void FrameEvents_Add(FrameEvents *events, uint8_t index, Score score, bool complete)
{
    assert(events->count < SCORER_MAX_EVENTS);
    FrameEvent *event = &events->events[events->count];
    event->frame_number = (FrameNumber)(index + 1U);
    event->frame_score = score;
    event->frame_complete = complete;
    events->count++;
}

static GameStatus Lane_CheckBall(const Lane *lane, Pins pins)
{
    if (lane->phase == LANE_OVER) {
        return GAME_ERR_GAME_OVER;
    }
    if (pins > lane->standing) {
        return GAME_ERR_INVALID_PINS;
    }
    return GAME_OK;
}

static void Scorer_AddBall(Scorer *self, Pins pins)
{
    self->balls[self->ball_count] = pins;
    self->ball_count++;
}

static void Scorer_ReportNewlyComplete(const Scorer *self, uint8_t were_complete,
                                       FrameEvents *events)
{
    const Lane lane = Scorer_Lane(self);
    const uint8_t now_complete = Lane_CountCompleteFrames(&lane, self->ball_count);
    for (uint8_t i = were_complete; i < now_complete; i++) {
        FrameEvents_Add(events, i, Lane_FrameScore(&lane, &lane.frames[i]), true);
    }
}

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events)
{
    events->count = 0U;
    const Lane before = Scorer_Lane(self);
    const GameStatus checked = Lane_CheckBall(&before, pins);
    if (checked != GAME_OK) {
        return checked;
    }
    const uint8_t were_complete = Lane_CountCompleteFrames(&before, self->ball_count);
    Scorer_AddBall(self, pins);
    Scorer_ReportNewlyComplete(self, were_complete, events);
    return GAME_OK;
}

Score Scorer_Score(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    Score score = 0U;
    for (uint8_t i = 0U; i < lane.frames_started; i++) {
        if (FrameShape_IsComplete(&lane.frames[i], self->ball_count)) {
            score = (Score)(score + Lane_FrameScore(&lane, &lane.frames[i]));
        }
    }
    return score;
}

static bool RollEdit_StartsAtABall(const RollEdit *edit, uint8_t ball_count)
{
    return (edit->first_roll != 0U) && (edit->first_roll <= ball_count);
}

static bool RollEdit_RemovesOnlyBallsThere(const RollEdit *edit, uint8_t ball_count)
{
    return ((unsigned)edit->first_roll + edit->rolls_removed) <= (ball_count + 1U);
}

static bool RollEdit_PromisesBallsWithoutPins(const RollEdit *edit)
{
    return (edit->new_pins == NULL) && (edit->new_count > 0U);
}

static unsigned RollEdit_BallsAfter(const RollEdit *edit, uint8_t ball_count)
{
    assert(edit->rolls_removed <= ball_count);
    return ((unsigned)ball_count - edit->rolls_removed) + edit->new_count;
}

static GameStatus Scorer_CheckEdit(const Scorer *self, const RollEdit *edit)
{
    if (edit == NULL) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (!RollEdit_StartsAtABall(edit, self->ball_count) ||
        !RollEdit_RemovesOnlyBallsThere(edit, self->ball_count)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (RollEdit_PromisesBallsWithoutPins(edit)) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (RollEdit_BallsAfter(edit, self->ball_count) > Scorer_Rules(self)->max_balls_per_game) {
        return GAME_ERR_TOO_MANY_ROLLS;
    }
    return GAME_OK;
}

static Pins Scorer_EditedBall(const Scorer *self, const RollEdit *edit, uint8_t index)
{
    const uint8_t new_from = (uint8_t)(edit->first_roll - 1U);
    const uint8_t new_until = (uint8_t)(new_from + edit->new_count);
    if (index < new_from) {
        return self->balls[index];
    }
    if (index < new_until) {
        return edit->new_pins[index - new_from];
    }
    const uint8_t after_removed = (uint8_t)(new_from + edit->rolls_removed);
    return self->balls[(uint8_t)(after_removed + (index - new_until))];
}

static GameStatus Scorer_ReplayEdited(const Scorer *self, const RollEdit *edit, Scorer *edited)
{
    FrameEvents ignored;
    Scorer_InitWithRule(edited, self->variant, self->rule);
    const uint8_t count = (uint8_t)RollEdit_BallsAfter(edit, self->ball_count);
    for (uint8_t i = 0U; i < count; i++) {
        const GameStatus status = Scorer_Roll(edited, Scorer_EditedBall(self, edit, i), &ignored);
        if (status != GAME_OK) {
            return status;
        }
    }
    return GAME_OK;
}

static void Scorer_ReportEveryFrameAgain(const Scorer *self, uint8_t were_complete,
                                         FrameEvents *events)
{
    const Lane lane = Scorer_Lane(self);
    const uint8_t now_complete = Lane_CountCompleteFrames(&lane, self->ball_count);
    const uint8_t frames_to_report = (were_complete > now_complete) ? were_complete : now_complete;
    for (uint8_t i = 0U; i < frames_to_report; i++) {
        if (i < now_complete) {
            FrameEvents_Add(events, i, Lane_FrameScore(&lane, &lane.frames[i]), true);
        } else {
            FrameEvents_Add(events, i, 0U, false);
        }
    }
}

GameStatus Scorer_Edit(Scorer *self, const RollEdit *edit, FrameEvents *events)
{
    events->count = 0U;
    const GameStatus checked = Scorer_CheckEdit(self, edit);
    if (checked != GAME_OK) {
        return checked;
    }
    const uint8_t were_complete = Scorer_CountCompleteFrames(self);
    Scorer edited;
    const GameStatus replayed = Scorer_ReplayEdited(self, edit, &edited);
    if (replayed != GAME_OK) {
        return replayed;
    }
    *self = edited;
    Scorer_ReportEveryFrameAgain(self, were_complete, events);
    return GAME_OK;
}

uint8_t Scorer_BallCount(const Scorer *self)
{
    return self->ball_count;
}

uint8_t Scorer_FramesStarted(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return lane.frames_started;
}

ScorerFrame Scorer_Frame(const Scorer *self, uint8_t index)
{
    const Lane lane = Scorer_Lane(self);
    ScorerFrame result = { 0U, false };
    if (index >= lane.frames_started) {
        return result;
    }
    const FrameShape *frame = &lane.frames[index];
    result.complete = FrameShape_IsComplete(frame, self->ball_count);
    if (result.complete) {
        result.score = Lane_FrameScore(&lane, frame);
    }
    return result;
}

Pins Scorer_PinsStanding(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return lane.standing;
}

bool Scorer_IsOver(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return lane.phase == LANE_OVER;
}
