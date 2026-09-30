#include "scorer.h"

#include "roll_edit.h"

#include <assert.h>
#include <stddef.h>

/* A frame scores at most every ball of the game, so no total can outgrow a Score. */
_Static_assert((SCORER_MAX_FRAMES * SCORER_MAX_BALLS * SCORER_MAX_PINS_PER_RACK) <= UINT16_MAX,
               "the highest possible total fits a Score");

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

static const ScorerRules *Scorer_Rules(const Scorer *self)
{
    return &self->rules;
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

static ScorerClearingBall FrameShape_ClearingBall(const FrameShape *frame)
{
    return (ScorerClearingBall)(frame->own_balls - 1U);
}

static void FrameShape_CloseClearingTheRack(FrameShape *frame, const ScorerRules *rules)
{
    frame->closed = true;
    frame->bonus_balls = rules->bonus_balls_by_clearing_ball[FrameShape_ClearingBall(frame)];
}

static bool Lane_IsLastFrame(const Lane *lane, const ScorerRules *rules)
{
    return lane->frames_started == rules->frames;
}

static void Lane_ResetRack(Lane *lane, const ScorerRules *rules)
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

static void Lane_AfterFrameCloses(Lane *lane, const ScorerRules *rules, const FrameShape *frame)
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

static void FrameShape_CloseIfDone(FrameShape *frame, const ScorerRules *rules, bool cleared)
{
    if (cleared) {
        FrameShape_CloseClearingTheRack(frame, rules);
    } else if (frame->own_balls == rules->balls_per_frame) {
        frame->closed = true;
    }
}

static void Lane_ThrowInFrame(Lane *lane, const ScorerRules *rules, uint8_t ball_index, Pins pins)
{
    FrameShape *frame = Lane_FrameTakingBall(lane, ball_index);
    FrameShape_TakeBall(frame);
    Lane_KnockDown(lane, pins);
    FrameShape_CloseIfDone(frame, rules, Lane_IsRackCleared(lane));
    if (frame->closed) {
        Lane_AfterFrameCloses(lane, rules, frame);
    }
}

static void Lane_ThrowFillBall(Lane *lane, const ScorerRules *rules, Pins pins)
{
    Lane_KnockDown(lane, pins);
    if (Lane_IsRackCleared(lane)) {
        Lane_ResetRack(lane, rules);
    }
    Lane_UseFillBall(lane);
}

static void Lane_Throw(Lane *lane, const ScorerRules *rules, uint8_t ball_index, Pins pins)
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
        assert(false && "Lane_CheckBall refuses a ball once the lane is over");
        break;
    }
}

/* Off a full rack, few enough left standing to count as clearing it. */
static bool ScorerRules_CountsAsAClear(const ScorerRules *rules, Pins standing, Pins pins)
{
    return (standing == rules->pins_per_rack) &&
           ((Pins)(standing - pins) <= rules->pins_standing_that_count_as_a_clear);
}

static Pins Scorer_CountPins(const Scorer *self, Pins standing, Pins pins)
{
    return ScorerRules_CountsAsAClear(Scorer_Rules(self), standing, pins) ? standing : pins;
}

static void Lane_Start(Lane *lane, const ScorerRules *rules)
{
    lane->frames_started = 0U;
    Lane_ResetRack(lane, rules);
    lane->phase = LANE_TAKING_FRAMES;
    lane->fill_balls_left = 0U;
}

static void Lane_TakeBall(Lane *lane, const ScorerRules *rules, uint8_t ball_index, Pins counted)
{
    lane->counted_pins[ball_index] = counted;
    Lane_Throw(lane, rules, ball_index, counted);
}

static Lane Scorer_Lane(const Scorer *self)
{
    const ScorerRules *rules = Scorer_Rules(self);
    Lane lane;
    Lane_Start(&lane, rules);
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        Lane_TakeBall(&lane, rules, i, Scorer_CountPins(self, lane.standing, self->balls[i]));
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

GameStatus Scorer_Start(Scorer *self, const ScorerRules *rules)
{
    if (!ScorerRules_AreValid(rules)) {
        return GAME_ERR_INVALID_RULES;
    }
    self->rules = *rules;
    self->max_balls = (uint8_t)ScorerRules_LongestGame(rules);
    self->ball_count = 0U;
    return GAME_OK;
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

static void FrameEvents_Clear(FrameEvents *events)
{
    events->count = 0U;
}

static void Lane_ReportComplete(const Lane *lane, uint8_t from, uint8_t until,
                                FrameEvents *events)
{
    for (uint8_t i = from; i < until; i++) {
        FrameEvents_Add(events, i, Lane_FrameScore(lane, &lane->frames[i]), true);
    }
}

static void FrameEvents_AddReopened(FrameEvents *events, uint8_t from, uint8_t until)
{
    for (uint8_t i = from; i < until; i++) {
        FrameEvents_Add(events, i, 0U, false);
    }
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
    Lane_ReportComplete(&lane, were_complete, now_complete, events);
}

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events)
{
    FrameEvents_Clear(events);
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

static Score Lane_TotalOfFirstFrames(const Lane *lane, uint8_t frames)
{
    Score total = 0U;
    for (uint8_t i = 0U; i < frames; i++) {
        total = (Score)(total + Lane_FrameScore(lane, &lane->frames[i]));
    }
    return total;
}

Score Scorer_Score(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return Lane_TotalOfFirstFrames(&lane, Lane_CountCompleteFrames(&lane, self->ball_count));
}

static bool Scorer_IsLongerThanAGame(const Scorer *self, unsigned ball_count)
{
    return ball_count > self->max_balls;
}

static GameStatus Scorer_CheckEdit(const Scorer *self, const RollEdit *edit)
{
    if (edit == NULL) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (!RollEdit_IsWithinBalls(edit, self->ball_count)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (RollEdit_PromisesBallsWithoutPins(edit)) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (Scorer_IsLongerThanAGame(self, RollEdit_BallsAfter(edit, self->ball_count))) {
        return GAME_ERR_TOO_MANY_ROLLS;
    }
    return GAME_OK;
}

static GameStatus Scorer_ReplayEdited(const Scorer *self, const RollEdit *edit, Scorer *edited)
{
    FrameEvents ignored;
    *edited = *self;
    edited->ball_count = 0U;
    const uint8_t count = (uint8_t)RollEdit_BallsAfter(edit, self->ball_count);
    for (uint8_t i = 0U; i < count; i++) {
        const Pins pins = RollEdit_Ball(edit, self->balls, i);
        const GameStatus status = Scorer_Roll(edited, pins, &ignored);
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
    Lane_ReportComplete(&lane, 0U, now_complete, events);
    FrameEvents_AddReopened(events, now_complete, were_complete);
}

GameStatus Scorer_Edit(Scorer *self, const RollEdit *edit, FrameEvents *events)
{
    FrameEvents_Clear(events);
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
    ScorerFrame frame = { 0U, false };
    if (index < Lane_CountCompleteFrames(&lane, self->ball_count)) {
        frame.score = Lane_FrameScore(&lane, &lane.frames[index]);
        frame.complete = true;
    }
    return frame;
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
