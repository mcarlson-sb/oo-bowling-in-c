#include "scoreboard.h"

/* The scoreboard's protocol, Smalltalk's respondsTo:: the selectors it answers. Any other reads as
 * SCOREBOARD_DOES_NOT_UNDERSTAND. */
typedef enum {
    SCOREBOARD_DOES_NOT_UNDERSTAND = 0,
    SCOREBOARD_FRAME_CHANGED,
    SCOREBOARD_QUERY_SCORE
} ScoreboardRequest;

static const ScoreboardRequest k_scoreboard_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = SCOREBOARD_FRAME_CHANGED,
    [MSG_QUERY_SCORE] = SCOREBOARD_QUERY_SCORE,
};

void Scoreboard_Init(Scoreboard *self, ActorId id)
{
    self->id = id;
    self->not_understood = 0U;
    for (uint8_t i = 0U; i < SCORER_MAX_FRAMES; i++) {
        self->scores[i] = 0U;
        self->complete[i] = false;
    }
}

static ScoreboardRequest Scoreboard_RequestOf(const Message *message)
{
    const Selector selector = message->envelope.selector;
    if ((unsigned)selector >= MSG_SELECTOR_COUNT) {
        return SCOREBOARD_DOES_NOT_UNDERSTAND;
    }
    return k_scoreboard_protocol[selector];
}

static void Scoreboard_Hear(Scoreboard *self, const FrameEvent *frame)
{
    const uint8_t index = (uint8_t)(frame->frame_number - 1U);
    self->scores[index] = frame->frame_score;
    self->complete[index] = frame->frame_complete;
}

static Score Scoreboard_Total(const Scoreboard *self)
{
    Score total = 0U;
    for (uint8_t i = 0U; i < SCORER_MAX_FRAMES; i++) {
        if (self->complete[i]) {
            total = (Score)(total + self->scores[i]);
        }
    }
    return total;
}

void Scoreboard_Handle(Scoreboard *self, const Message *message, Outbox *outbox)
{
    switch (Scoreboard_RequestOf(message)) {
    case SCOREBOARD_FRAME_CHANGED:
        Scoreboard_Hear(self, &message->payload.frame);
        break;
    case SCOREBOARD_QUERY_SCORE:
        Outbox_Reply(outbox, message, GAME_OK, Scoreboard_Total(self));
        break;
    case SCOREBOARD_DOES_NOT_UNDERSTAND:
        self->not_understood++;
        Outbox_NotUnderstood(outbox, self->id, message);
        break;
    }
}
