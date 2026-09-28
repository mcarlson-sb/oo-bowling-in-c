#include "scorer.h"

#include <stddef.h>

/* A frame takes at most this many balls of its own, in any variant. */
#define SCORER_MAX_BALLS_PER_FRAME 3U

/* Everything that differs between variants: data, not code. */
typedef struct {
    uint8_t frames;
    uint8_t balls_per_frame;
    Pins pins_per_rack;
    uint8_t max_balls; /* in a whole game */
    /* Bonus balls owed by a frame that clears the rack, by the ball that cleared it: [0] is the
     * first ball (a strike), [1] the second (a spare). */
    uint8_t bonus_balls[SCORER_MAX_BALLS_PER_FRAME];
} VariantRules;

static const VariantRules k_variant_rules[] = {
    [SCORER_TEN_PIN] = { .frames = 10U, .balls_per_frame = 2U, .pins_per_rack = 10U,
                         .max_balls = 21U, .bonus_balls = { 2U, 1U, 0U } },
};

/* Where a frame's balls are in the game's list, and how many bonus balls it is owed. */
typedef struct {
    uint8_t first_ball;
    uint8_t balls;
    uint8_t bonus_balls;
    bool ended; /* it takes no more balls of its own */
} FrameShape;

/* What the lane takes next. The last frame's bonus balls are fill balls: they are thrown after
 * it, on a fresh rack whenever one is cleared, but they start no frame. */
typedef enum {
    LANE_TAKING_FRAMES,
    LANE_TAKING_FILL_BALLS,
    LANE_OVER
} LanePhase;

/* The frames, worked out by walking the balls from the start. */
typedef struct {
    FrameShape frames[SCORER_MAX_FRAMES];
    uint8_t frame_count; /* started */
    Pins counted[SCORER_MAX_BALLS]; /* each ball, as its rule counts it */
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
    return (lane->frame_count == 0U) || lane->frames[lane->frame_count - 1U].ended;
}

static FrameShape *Lane_StartFrame(Lane *lane, const VariantRules *rules, uint8_t ball_index)
{
    FrameShape *frame = &lane->frames[lane->frame_count];
    frame->first_ball = ball_index;
    frame->balls = 0U;
    frame->bonus_balls = 0U;
    frame->ended = false;
    lane->frame_count++;
    lane->standing = rules->pins_per_rack;
    return frame;
}

static FrameShape *Lane_CurrentFrame(Lane *lane, const VariantRules *rules, uint8_t ball_index)
{
    if (Lane_NeedsNewFrame(lane)) {
        return Lane_StartFrame(lane, rules, ball_index);
    }
    return &lane->frames[lane->frame_count - 1U];
}

static bool Lane_IsLastFrame(const Lane *lane, const VariantRules *rules)
{
    return lane->frame_count == rules->frames;
}

/* After the frame it belongs to ends: the next frame, the last frame's fill balls, or the end. */
static void Lane_AfterFrameEnds(Lane *lane, const VariantRules *rules, const FrameShape *frame)
{
    lane->standing = rules->pins_per_rack;
    if (!Lane_IsLastFrame(lane, rules)) {
        return;
    }
    lane->fill_balls_left = frame->bonus_balls;
    lane->phase = (frame->bonus_balls > 0U) ? LANE_TAKING_FILL_BALLS : LANE_OVER;
}

static void Lane_ThrowInFrame(Lane *lane, const VariantRules *rules, uint8_t ball_index, Pins pins)
{
    FrameShape *frame = Lane_CurrentFrame(lane, rules, ball_index);
    frame->balls++;
    lane->standing = (Pins)(lane->standing - pins);
    if (lane->standing == 0U) {
        frame->ended = true;
        frame->bonus_balls = rules->bonus_balls[frame->balls - 1U];
    } else if (frame->balls == rules->balls_per_frame) {
        frame->ended = true;
    }
    if (frame->ended) {
        Lane_AfterFrameEnds(lane, rules, frame);
    }
}

static void Lane_ThrowFillBall(Lane *lane, const VariantRules *rules, Pins pins)
{
    lane->standing = (Pins)(lane->standing - pins);
    if (lane->standing == 0U) {
        lane->standing = rules->pins_per_rack;
    }
    lane->fill_balls_left--;
    if (lane->fill_balls_left == 0U) {
        lane->phase = LANE_OVER;
    }
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
        break; /* Scorer_Roll refuses a ball once the lane is over */
    }
}

static Pins Scorer_Count(const Scorer *self, Pins standing, Pins pins)
{
    switch (self->rule) {
    case SCORER_COUNT_NO_TAP:
        return ((standing == Scorer_Rules(self)->pins_per_rack) && ((pins + 1U) == standing))
                   ? standing
                   : pins;
    case SCORER_COUNT_PINS_DOWN:
    default:
        return pins;
    }
}

static void Lane_Walk(Lane *lane, const Scorer *self)
{
    const VariantRules *rules = Scorer_Rules(self);
    lane->frame_count = 0U;
    lane->standing = rules->pins_per_rack;
    lane->phase = LANE_TAKING_FRAMES;
    lane->fill_balls_left = 0U;
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        lane->counted[i] = Scorer_Count(self, lane->standing, self->balls[i]);
        Lane_Throw(lane, rules, i, lane->counted[i]);
    }
}

/* A frame's balls, then its bonus balls: one rule for open frames, spares and strikes. */
static uint8_t FrameShape_BallsScored(const FrameShape *frame)
{
    return (uint8_t)(frame->balls + frame->bonus_balls);
}

static bool Scorer_IsFrameComplete(const Scorer *self, const FrameShape *frame)
{
    return frame->ended &&
           (self->ball_count >= (uint8_t)(frame->first_ball + FrameShape_BallsScored(frame)));
}

static Score Lane_FrameScore(const Lane *lane, const FrameShape *frame)
{
    Score score = 0U;
    const uint8_t end = (uint8_t)(frame->first_ball + FrameShape_BallsScored(frame));
    for (uint8_t i = frame->first_ball; i < end; i++) {
        score = (Score)(score + lane->counted[i]);
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

/* How many frames are complete. They always complete oldest first: a frame's bonus balls are
 * the next frames' own balls, and no frame completes before the bonus balls of the one before
 * it are in. So the complete frames are always the first few. */
static uint8_t Scorer_CompleteFrames(const Scorer *self, const Lane *lane)
{
    uint8_t complete = 0U;
    while ((complete < lane->frame_count) &&
           Scorer_IsFrameComplete(self, &lane->frames[complete])) {
        complete++;
    }
    return complete;
}

static uint8_t Scorer_CompleteFrameCount(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    return Scorer_CompleteFrames(self, &lane);
}

static void FrameEvents_Add(FrameEvents *events, uint8_t index, Score score, bool complete)
{
    FrameEvent *event = &events->events[events->count];
    event->frame_number = (FrameNumber)(index + 1U);
    event->frame_score = score;
    event->frame_complete = complete;
    events->count++;
}

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events)
{
    events->count = 0U;
    Lane lane;
    Lane_Walk(&lane, self);
    if (lane.phase == LANE_OVER) {
        return GAME_ERR_GAME_OVER;
    }
    if (pins > lane.standing) {
        return GAME_ERR_INVALID_PINS;
    }
    const uint8_t were_complete = Scorer_CompleteFrames(self, &lane);
    self->balls[self->ball_count] = pins;
    self->ball_count++;

    Lane_Walk(&lane, self);
    const uint8_t now_complete = Scorer_CompleteFrames(self, &lane);
    for (uint8_t i = were_complete; i < now_complete; i++) {
        FrameEvents_Add(events, i, Lane_FrameScore(&lane, &lane.frames[i]), true);
    }
    return GAME_OK;
}

Score Scorer_Score(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    Score score = 0U;
    for (uint8_t i = 0U; i < lane.frame_count; i++) {
        if (Scorer_IsFrameComplete(self, &lane.frames[i])) {
            score = (Score)(score + Lane_FrameScore(&lane, &lane.frames[i]));
        }
    }
    return score;
}

/* ---- Edits ------------------------------------------------------------------------------ */

static bool Scorer_EditStartsAtABall(const Scorer *self, const RollEdit *edit)
{
    return (edit->first_roll != 0U) && (edit->first_roll <= self->ball_count);
}

/* Only once it starts at a ball: ball 0 has no index. */
static bool Scorer_EditRemovesOnlyExistingBalls(const Scorer *self, const RollEdit *edit)
{
    return ((unsigned)(edit->first_roll - 1U) + edit->rolls_removed) <= self->ball_count;
}

static bool RollEdit_PromisesBallsWithoutPins(const RollEdit *edit)
{
    return (edit->new_pins == NULL) && (edit->new_count > 0U);
}

/* Only once the edit is within the balls: more removed than there are would wrap. */
static unsigned Scorer_BallsAfterEdit(const Scorer *self, const RollEdit *edit)
{
    return ((unsigned)self->ball_count - edit->rolls_removed) + edit->new_count;
}

/* The same rules, in the same order, as the Game facade's edits. */
static GameStatus Scorer_CheckEdit(const Scorer *self, const RollEdit *edit)
{
    if (edit == NULL) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (!Scorer_EditStartsAtABall(self, edit) || !Scorer_EditRemovesOnlyExistingBalls(self, edit)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (RollEdit_PromisesBallsWithoutPins(edit)) {
        return GAME_ERR_INVALID_EDIT;
    }
    if (Scorer_BallsAfterEdit(self, edit) > Scorer_Rules(self)->max_balls) {
        return GAME_ERR_TOO_MANY_ROLLS;
    }
    return GAME_OK;
}

/* The ball at `index` of the edited game: before the range, the new balls, then after it. */
static Pins Scorer_EditedBall(const Scorer *self, const RollEdit *edit, uint8_t index)
{
    const uint8_t first = (uint8_t)(edit->first_roll - 1U);
    if (index < first) {
        return self->balls[index];
    }
    if (index < (uint8_t)(first + edit->new_count)) {
        return edit->new_pins[index - first];
    }
    return self->balls[(uint8_t)(index - edit->new_count + edit->rolls_removed)];
}

/* Every ball of the edited game into a fresh copy, each judged as a roll would be. */
static GameStatus Scorer_ReplayEdited(const Scorer *self, const RollEdit *edit, Scorer *edited)
{
    FrameEvents ignored;
    Scorer_InitWithRule(edited, self->variant, self->rule);
    const uint8_t count = (uint8_t)Scorer_BallsAfterEdit(self, edit);
    for (uint8_t i = 0U; i < count; i++) {
        const GameStatus status = Scorer_Roll(edited, Scorer_EditedBall(self, edit, i), &ignored);
        if (status != GAME_OK) {
            return status;
        }
    }
    return GAME_OK;
}

/* After an edit, every frame again: a complete one with its score, and one that was complete
 * but no longer is with complete = false. */
static void Scorer_ReportAll(const Scorer *self, uint8_t were_complete, FrameEvents *events)
{
    Lane lane;
    Lane_Walk(&lane, self);
    const uint8_t now_complete = Scorer_CompleteFrames(self, &lane);
    const uint8_t frames = (were_complete > now_complete) ? were_complete : now_complete;
    for (uint8_t i = 0U; i < frames; i++) {
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
    const uint8_t were_complete = Scorer_CompleteFrameCount(self);
    Scorer edited;
    const GameStatus replayed = Scorer_ReplayEdited(self, edit, &edited);
    if (replayed != GAME_OK) {
        return replayed; /* self was never touched: nothing to undo */
    }
    *self = edited; /* a plain value, so a copy is the whole game */
    Scorer_ReportAll(self, were_complete, events);
    return GAME_OK;
}

/* ---- Questions ------------------------------------------------------------------------- */

uint8_t Scorer_FrameCount(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    return lane.frame_count;
}

ScorerFrame Scorer_Frame(const Scorer *self, uint8_t index)
{
    Lane lane;
    Lane_Walk(&lane, self);
    const FrameShape *frame = &lane.frames[index];
    ScorerFrame result = { 0U, Scorer_IsFrameComplete(self, frame) };
    if (result.complete) {
        result.score = Lane_FrameScore(&lane, frame);
    }
    return result;
}

Pins Scorer_PinsStanding(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    return lane.standing;
}

bool Scorer_IsOver(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    return lane.phase == LANE_OVER;
}
