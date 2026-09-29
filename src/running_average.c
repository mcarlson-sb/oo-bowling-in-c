#include "running_average.h"

/* The running average's protocol, Smalltalk's respondsTo:: the selectors it answers. Any other
 * reads as AVERAGE_DOES_NOT_UNDERSTAND. */
typedef enum {
    AVERAGE_DOES_NOT_UNDERSTAND = 0,
    AVERAGE_FRAME_CHANGED,
    AVERAGE_QUERY_SCORE
} AverageRequest;

static const AverageRequest k_average_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = AVERAGE_FRAME_CHANGED,
    [MSG_QUERY_SCORE] = AVERAGE_QUERY_SCORE,
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
    if ((unsigned)selector >= MSG_SELECTOR_COUNT) {
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

void RunningAverage_Handle(RunningAverage *self, const Message *message, Outbox *outbox)
{
    switch (RunningAverage_RequestOf(message)) {
    case AVERAGE_FRAME_CHANGED:
        FrameBoard_Hear(&self->board, &message->payload.frame);
        break;
    case AVERAGE_QUERY_SCORE:
        Outbox_Reply(outbox, message, GAME_OK, RunningAverage_Average(self));
        break;
    case AVERAGE_DOES_NOT_UNDERSTAND:
        self->not_understood++;
        Outbox_NotUnderstood(outbox, self->id, message);
        break;
    }
}
