#include "running_average.h"
#include "running_average_state.h"

/* The running average's protocol, Smalltalk's respondsTo:: the selectors it answers. Any other
 * reads as AVERAGE_DOES_NOT_UNDERSTAND. */
typedef enum {
    AVERAGE_DOES_NOT_UNDERSTAND = 0,
    AVERAGE_FRAME_CHANGED,
    AVERAGE_QUERY_FIGURE,
    AVERAGE_QUERY_STATS,
    AVERAGE_NOTHING_TO_DO
} AverageRequest;

static const AverageRequest k_average_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = AVERAGE_FRAME_CHANGED,
    [MSG_QUERY_FIGURE] = AVERAGE_QUERY_FIGURE,
    [MSG_QUERY_STATS] = AVERAGE_QUERY_STATS,
    /* What a game tells every subscriber: heard, and nothing to do. */
    [MSG_REPLY] = AVERAGE_NOTHING_TO_DO,
    [MSG_ROLL_HELD] = AVERAGE_NOTHING_TO_DO,
    [MSG_ROLLS_LOST] = AVERAGE_NOTHING_TO_DO,
};

void RunningAverage_Init(RunningAverage *self, ActorId id)
{
    self->id = id;
    self->not_understood = 0U;
    FrameBoard_Init(&self->board);
}

static AverageRequest RunningAverage_RequestOf(const Message *message)
{
    const Selector selector = message->envelope.selector;
    if (!Selector_IsInProtocol(selector)) {
        return AVERAGE_DOES_NOT_UNDERSTAND;
    }
    return k_average_protocol[selector];
}

/* Of the complete frames, rounded down; 0 before any is complete. */
static Score RunningAverage_Average(const RunningAverage *self)
{
    const uint8_t complete = FrameBoard_CompleteCount(&self->board);
    if (complete == 0U) {
        return 0U;
    }
    return (Score)(FrameBoard_Total(&self->board) / complete);
}

/* The facts behind the average, its total and its complete frames, unrounded. */
static void RunningAverage_AnswerStats(const RunningAverage *self, const Message *message,
                                       Outbox *outbox)
{
    StatsPayload *stats = Outbox_BeginStats(outbox, message);
    stats->not_understood = self->not_understood;
    stats->complete_frames = FrameBoard_CompleteCount(&self->board);
    stats->total = FrameBoard_Total(&self->board);
}

void RunningAverage_Handle(RunningAverage *self, const Message *message, Outbox *outbox)
{
    switch (RunningAverage_RequestOf(message)) {
    case AVERAGE_FRAME_CHANGED:
        FrameBoard_Hear(&self->board, &message->payload.frame);
        break;
    case AVERAGE_QUERY_FIGURE:
        Outbox_Reply(outbox, message, GAME_OK, RunningAverage_Average(self));
        break;
    case AVERAGE_QUERY_STATS:
        RunningAverage_AnswerStats(self, message, outbox);
        break;
    case AVERAGE_NOTHING_TO_DO:
        break;
    case AVERAGE_DOES_NOT_UNDERSTAND:
        self->not_understood++;
        Outbox_NotUnderstood(outbox, self->id, message);
        break;
    }
}
