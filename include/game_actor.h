#ifndef GAME_ACTOR_H
#define GAME_ACTOR_H

/* The game actor: one game, and everything that may change it, as messages. It handles one
 * message at a time and writes what it sends, replies and events, to an outbox the caller
 * supplies. It is pure: no RTOS, no callbacks, no function pointers. The RTOS shell owns the one
 * instance, and moves messages between it and the queues.
 *
 * A reply address (reply_to) is opaque here: the shell's queue, copied onto each output bound
 * for it, never followed. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_status.h"
#include "bowling_types.h"
#include "scorer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Chosen by the caller, and echoed in the reply, so it can have several requests in flight. */
typedef uint16_t RequestSeq;

typedef enum {
    GAME_MSG_ROLL,
    /* reply_to is the subscriber: it gets the reply, a catch-up of the complete frames, then
     * every frame change. */
    GAME_MSG_SUBSCRIBE
} GameMessageKind;

typedef struct {
    GameMessageKind kind;
    RequestSeq seq;
    void *reply_to;
    Pins pins;
} GameMessage;

typedef enum {
    GAME_OUT_REPLY,        /* seq, status, score */
    GAME_OUT_FRAME_CHANGED /* frame */
} GameOutputKind;

typedef struct {
    GameOutputKind kind;
    void *to;
    RequestSeq seq;
    GameStatus status;
    Score score;
    FrameEvent frame;
} GameOutput;

#define GAME_MAX_SUBSCRIBERS 2U

/* The most events one message can cause, for each subscriber: an edit's frames, and the frames
 * the rolls it lets through complete, and a notice or two. */
#define GAME_EVENTS_PER_MESSAGE ((2U * SCORER_MAX_EVENTS) + 2U)

/* A reply, and each subscriber's events. */
#define GAME_OUTBOX_CAPACITY (1U + (GAME_MAX_SUBSCRIBERS * GAME_EVENTS_PER_MESSAGE))

/* What one message sent: replies and events, in the order they were sent. */
typedef struct {
    GameOutput items[GAME_OUTBOX_CAPACITY];
    uint8_t count;
} GameOutbox;

typedef struct {
    Scorer scorer;
    void *subscribers[GAME_MAX_SUBSCRIBERS];
    uint8_t subscriber_count;
} GameActor;

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule);

/* Handles one message. The outbox is emptied first, then filled. */
void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_H */
