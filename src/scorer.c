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

/* The most balls a game can take: every frame's own balls, and the last frame's fill balls. */
#define TEN_PIN_MAX_BALLS 21U   /* 9 frames of 2, then 3 in the tenth */
#define CANDLEPIN_MAX_BALLS 30U /* 10 frames of 3: a strike or a spare in the tenth takes fill
                                 * balls up to the same 3 */

_Static_assert(TEN_PIN_MAX_BALLS <= SCORER_MAX_BALLS, "ten-pin must fit the scorer's storage");
_Static_assert(CANDLEPIN_MAX_BALLS <= SCORER_MAX_BALLS, "candlepin must fit the scorer's storage");

static const VariantRules k_variant_rules[] = {
    [SCORER_TEN_PIN] = { .frames = 10U, .balls_per_frame = 2U, .pins_per_rack = 10U,
                         .max_balls = TEN_PIN_MAX_BALLS, .bonus_balls = { 2U, 1U, 0U } },
    /* The third ball can clear the rack too, a ten-box, which earns no bonus. */
    [SCORER_CANDLEPIN] = { .frames = 10U, .balls_per_frame = 3U, .pins_per_rack = 10U,
                           .max_balls = CANDLEPIN_MAX_BALLS, .bonus_balls = { 2U, 1U, 0U } },
};

/* Where a frame's balls are in the game's list, and how many bonus balls it is owed. A closed
 * frame takes no more balls of its own; it is complete once its bonus balls are in too. */
typedef struct {
    uint8_t first_ball;
    uint8_t own_balls;
    uint8_t bonus_balls;
    bool closed;
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
    return (lane->frame_count == 0U) || lane->frames[lane->frame_count - 1U].closed;
}

static FrameShape *Lane_StartFrame(Lane *lane, const VariantRules *rules, uint8_t ball_index)
{
    FrameShape *frame = &lane->frames[lane->frame_count];
    frame->first_ball = ball_index;
    frame->own_balls = 0U;
    frame->bonus_balls = 0U;
    frame->closed = false;
    lane->frame_count++;
    lane->standing = rules->pins_per_rack;
    return frame;
}

static FrameShape *Lane_FrameTakingBall(Lane *lane, const VariantRules *rules, uint8_t ball_index)
{
    if (Lane_NeedsNewFrame(lane)) {
        return Lane_StartFrame(lane, rules, ball_index);
    }
    return &lane->frames[lane->frame_count - 1U];
}

/* A frame that clears the rack closes owing the bonus its clearing ball earns: a strike's,
 * a spare's, or, on candlepin's third ball, a ten-box's none. */
static void FrameShape_CloseClearingTheRack(FrameShape *frame, const VariantRules *rules)
{
    frame->closed = true;
    frame->bonus_balls = rules->bonus_balls[frame->own_balls - 1U];
}

static bool Lane_IsLastFrame(const Lane *lane, const VariantRules *rules)
{
    return lane->frame_count == rules->frames;
}

/* After a frame closes: the next frame, the last frame's fill balls, or the end of the game. */
static void Lane_AfterFrameCloses(Lane *lane, const VariantRules *rules, const FrameShape *frame)
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
    FrameShape *frame = Lane_FrameTakingBall(lane, rules, ball_index);
    frame->own_balls++;
    lane->standing = (Pins)(lane->standing - pins);
    if (lane->standing == 0U) {
        FrameShape_CloseClearingTheRack(frame, rules);
    } else if (frame->own_balls == rules->balls_per_frame) {
        frame->closed = true; /* an open frame: no bonus */
    }
    if (frame->closed) {
        Lane_AfterFrameCloses(lane, rules, frame);
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

/* The lane as it stands after every ball so far: the frames, the pins, and what it takes next.
 * Worked out from the balls each time, which is what keeps a Scorer a plain value. */
static Lane Scorer_Lane(const Scorer *self)
{
    const VariantRules *rules = Scorer_Rules(self);
    Lane lane;
    lane.frame_count = 0U;
    lane.standing = rules->pins_per_rack;
    lane.phase = LANE_TAKING_FRAMES;
    lane.fill_balls_left = 0U;
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        lane.counted[i] = Scorer_Count(self, lane.standing, self->balls[i]);
        Lane_Throw(&lane, rules, i, lane.counted[i]);
    }
    return lane;
}

/* Just past the last ball a frame scores: its own balls, then its bonus balls. One rule for open
 * frames, spares and strikes. */
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
static uint8_t Lane_CountCompleteFrames(const Lane *lane, uint8_t ball_count)
{
    uint8_t complete = 0U;
    while ((complete < lane->frame_count) &&
           FrameShape_IsComplete(&lane->frames[complete], ball_count)) {
        complete++;
    }
    return complete;
}

/* Its own lane, walked and gone before its caller walks another: two live at once is too much
 * stack for Scorer_Edit. */
static uint8_t Scorer_CountCompleteFrames(const Scorer *self)
{
    const Lane lane = Scorer_Lane(self);
    return Lane_CountCompleteFrames(&lane, self->ball_count);
}

static void FrameEvents_Add(FrameEvents *events, uint8_t index, Score score, bool complete)
{
    FrameEvent *event = &events->events[events->count];
    event->frame_number = (FrameNumber)(index + 1U);
    event->frame_score = score;
    event->frame_complete = complete;
    events->count++;
}

/* Whether the lane takes this ball: not once the game is over, nor more pins than stand. */
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

/* The frames that are complete now but weren't before, oldest first. */
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
    for (uint8_t i = 0U; i < lane.frame_count; i++) {
        if (FrameShape_IsComplete(&lane.frames[i], self->ball_count)) {
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
    const Lane lane = Scorer_Lane(self);
    const uint8_t now_complete = Lane_CountCompleteFrames(&lane, self->ball_count);
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
    const uint8_t were_complete = Scorer_CountCompleteFrames(self);
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
    const Lane lane = Scorer_Lane(self);
    return lane.frame_count;
}

ScorerFrame Scorer_Frame(const Scorer *self, uint8_t index)
{
    const Lane lane = Scorer_Lane(self);
    ScorerFrame result = { 0U, false };
    if (index >= lane.frame_count) {
        return result; /* not started: only the frames the walk wrote are read */
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
