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
    GAME_MSG_ROLL
} GameMessageKind;

typedef struct {
    GameMessageKind kind;
    RequestSeq seq;
    void *reply_to;
    Pins pins;
} GameMessage;

typedef enum {
    GAME_OUT_REPLY
} GameOutputKind;

typedef struct {
    GameOutputKind kind;
    void *to;
    RequestSeq seq;
    GameStatus status;
    Score score;
} GameOutput;

#define GAME_OUTBOX_CAPACITY 1U

/* What one message sent: replies and events, in the order they were sent. */
typedef struct {
    GameOutput items[GAME_OUTBOX_CAPACITY];
    uint8_t count;
} GameOutbox;

typedef struct {
    Scorer scorer;
} GameActor;

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule);

/* Handles one message. The outbox is emptied first, then filled. */
void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_H */
