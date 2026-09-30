#include "scoreboard.h"
#include "scoreboard_state.h"

/* The scoreboard's protocol, Smalltalk's respondsTo:: the selectors it answers. Any other reads as
 * SCOREBOARD_DOES_NOT_UNDERSTAND. */
typedef enum {
    SCOREBOARD_DOES_NOT_UNDERSTAND = 0,
    SCOREBOARD_FRAME_CHANGED,
    SCOREBOARD_QUERY_FIGURE,
    SCOREBOARD_QUERY_STATS,
    SCOREBOARD_NOTHING_TO_DO
} ScoreboardRequest;

static const ScoreboardRequest k_scoreboard_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = SCOREBOARD_FRAME_CHANGED,
    [MSG_QUERY_FIGURE] = SCOREBOARD_QUERY_FIGURE,
    [MSG_QUERY_STATS] = SCOREBOARD_QUERY_STATS,
    /* What a game tells every subscriber: heard, and nothing to do. */
    [MSG_REPLY] = SCOREBOARD_NOTHING_TO_DO,
    [MSG_ROLL_HELD] = SCOREBOARD_NOTHING_TO_DO,
    [MSG_ROLLS_LOST] = SCOREBOARD_NOTHING_TO_DO,
};

void Scoreboard_Init(Scoreboard *self)
{
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

static void Scoreboard_AnswerStats(const Scoreboard *self, const Message *message,
                                   Outbox *outbox)
{
    StatsPayload *stats = Outbox_BeginStats(outbox, message);
    stats->not_understood = self->not_understood;
    stats->complete_frames = FrameBoard_CompleteCount(&self->board);
    stats->total = FrameBoard_Total(&self->board);
}

void Scoreboard_Handle(Scoreboard *self, const Message *message, Outbox *outbox)
{
    switch (Scoreboard_RequestOf(message)) {
    case SCOREBOARD_FRAME_CHANGED:
        FrameBoard_Hear(&self->board, &message->payload.frame);
        break;
    case SCOREBOARD_QUERY_FIGURE:
        Outbox_Reply(outbox, message, REPLY_OK, FrameBoard_Total(&self->board));
        break;
    case SCOREBOARD_QUERY_STATS:
        Scoreboard_AnswerStats(self, message, outbox);
        break;
    case SCOREBOARD_NOTHING_TO_DO:
        break;
    case SCOREBOARD_DOES_NOT_UNDERSTAND:
        self->not_understood++;
        Outbox_NotUnderstood(outbox, message);
        break;
    }
}
