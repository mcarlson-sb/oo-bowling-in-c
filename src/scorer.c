#include "scorer.h"

/* A frame takes at most this many balls of its own, in any variant. */
#define SCORER_MAX_BALLS_PER_FRAME 3U

/* Everything that differs between variants: data, not code. */
typedef struct {
    uint8_t frames;
    uint8_t balls_per_frame;
    Pins pins_per_rack;
    /* Bonus balls owed by a frame that clears the rack, by the ball that cleared it: [0] is the
     * first ball (a strike), [1] the second (a spare). */
    uint8_t bonus_balls[SCORER_MAX_BALLS_PER_FRAME];
} VariantRules;

static const VariantRules k_variant_rules[] = {
    [SCORER_TEN_PIN] = { .frames = 10U, .balls_per_frame = 2U, .pins_per_rack = 10U,
                         .bonus_balls = { 2U, 1U, 0U } },
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
    if (!Lane_IsLastFrame(lane, rules)) {
        return;
    }
    lane->fill_balls_left = frame->bonus_balls;
    lane->phase = (frame->bonus_balls > 0U) ? LANE_TAKING_FILL_BALLS : LANE_OVER;
    lane->standing = rules->pins_per_rack;
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

static void Lane_Walk(Lane *lane, const Scorer *self)
{
    const VariantRules *rules = Scorer_Rules(self);
    lane->frame_count = 0U;
    lane->standing = rules->pins_per_rack;
    lane->phase = LANE_TAKING_FRAMES;
    lane->fill_balls_left = 0U;
    for (uint8_t i = 0U; i < self->ball_count; i++) {
        Lane_Throw(lane, rules, i, self->balls[i]);
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

static Score Scorer_FrameScore(const Scorer *self, const FrameShape *frame)
{
    Score score = 0U;
    const uint8_t end = (uint8_t)(frame->first_ball + FrameShape_BallsScored(frame));
    for (uint8_t i = frame->first_ball; i < end; i++) {
        score = (Score)(score + self->balls[i]);
    }
    return score;
}

void Scorer_Init(Scorer *self, ScorerVariant variant)
{
    self->variant = variant;
    self->ball_count = 0U;
}

GameStatus Scorer_Roll(Scorer *self, Pins pins, FrameEvents *events)
{
    events->count = 0U;
    Lane lane;
    Lane_Walk(&lane, self);
    if (lane.phase == LANE_OVER) {
        return GAME_ERR_GAME_OVER;
    }
    self->balls[self->ball_count] = pins;
    self->ball_count++;
    return GAME_OK;
}

Score Scorer_Score(const Scorer *self)
{
    Lane lane;
    Lane_Walk(&lane, self);
    Score score = 0U;
    for (uint8_t i = 0U; i < lane.frame_count; i++) {
        if (Scorer_IsFrameComplete(self, &lane.frames[i])) {
            score = (Score)(score + Scorer_FrameScore(self, &lane.frames[i]));
        }
    }
    return score;
}
