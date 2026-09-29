#include "scoreboard.h"
#include "scoreboard_state.h"

/* The scoreboard's protocol, Smalltalk's respondsTo:: the selectors it answers. Any other reads as
 * SCOREBOARD_DOES_NOT_UNDERSTAND. */
typedef enum {
    SCOREBOARD_DOES_NOT_UNDERSTAND = 0,
    SCOREBOARD_FRAME_CHANGED,
    SCOREBOARD_QUERY_SCORE,
    SCOREBOARD_REPLY
} ScoreboardRequest;

static const ScoreboardRequest k_scoreboard_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = SCOREBOARD_FRAME_CHANGED,
    [MSG_QUERY_SCORE] = SCOREBOARD_QUERY_SCORE,
    [MSG_REPLY] = SCOREBOARD_REPLY,
};

void Scoreboard_Init(Scoreboard *self, ActorId id)
{
    self->id = id;
    self->not_understood = 0U;
    FrameBoard_Init(&self->board);
}

static ScoreboardRequest Scoreboard_RequestOf(const Message *message)
{
    const Selector selector = message->envelope.selector;
    if (!Selector_IsInProtocol(selector)) {
        return SCOREBOARD_DOES_NOT_UNDERSTAND;
    }
    return k_scoreboard_protocol[selector];
}

void Scoreboard_Handle(Scoreboard *self, const Message *message, Outbox *outbox)
{
    switch (Scoreboard_RequestOf(message)) {
    case SCOREBOARD_FRAME_CHANGED:
        FrameBoard_Hear(&self->board, &message->payload.frame);
        break;
    case SCOREBOARD_QUERY_SCORE:
        Outbox_Reply(outbox, message, GAME_OK, FrameBoard_Total(&self->board));
        break;
    case SCOREBOARD_REPLY:
        break; /* to its subscription: nothing to do */
    case SCOREBOARD_DOES_NOT_UNDERSTAND:
        self->not_understood++;
        Outbox_NotUnderstood(outbox, self->id, message);
        break;
    }
}
