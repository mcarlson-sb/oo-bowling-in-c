#ifndef GAME_ACTOR_H
#define GAME_ACTOR_H

/* The game actor: one game, and everything that may change it, as messages. It handles one
 * message at a time and writes what it sends, replies and events, to an outbox the caller
 * supplies. It knows nothing of the RTOS: the RTOS shell owns the one instance, and moves
 * messages between it and the queues.
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
    GAME_MSG_SUBSCRIBE,
    GAME_MSG_UNSUBSCRIBE, /* reply_to is the subscriber, which gets the reply */
    /* An edit (see RollEdit), with its new balls carried in the message: a queue copies the
     * message, so nothing in it may point back into the sender's memory. A correction is an
     * edit of one ball out and one in. */
    GAME_MSG_EDIT,
    /* A roll the pinsetter counted, from its interrupt: no reply_to, and no reply. One the
     * game rejects is held, with every pinsetter roll after it, until a correction lets it
     * through; see GameActor. */
    GAME_MSG_PINSETTER_ROLL,
    GAME_MSG_QUERY_SCORE,
    /* Throws away the first held roll, a glitch, and lets the rest through as far as they go.
     * GAME_ERR_NO_SUCH_ROLL if nothing is held. */
    GAME_MSG_DISCARD_HELD,
    /* The pinsetter's count of rolls it lost to a full queue, so far (lost). */
    GAME_MSG_ROLLS_LOST
} GameSelector;

/* Who a message is from and what it asks: every message has one. */
typedef struct {
    GameSelector selector;
    RequestSeq seq;
    void *reply_to;
} GameEnvelope;

typedef struct {
    Pins pins;
} GameRollPayload;

typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    uint8_t new_count;
    Pins new_pins[SCORER_MAX_BALLS];
} GameEditPayload;

typedef struct {
    uint16_t lost;
} GameRollsLostPayload;

/* An envelope, and the fields of its selector only. */
typedef struct {
    GameEnvelope envelope;
    union {
        GameRollPayload roll;             /* GAME_MSG_ROLL, GAME_MSG_PINSETTER_ROLL */
        GameEditPayload edit;             /* GAME_MSG_EDIT */
        GameRollsLostPayload rolls_lost;  /* GAME_MSG_ROLLS_LOST */
    } payload;
} GameMessage;

typedef enum {
    /* seq and status; and score, after everything the message caused, for a Roll, an Edit, an
     * accepted DiscardHeld and a QueryScore (0 in every other reply) */
    GAME_OUT_REPLY,
    GAME_OUT_FRAME_CHANGED, /* frame */
    /* A pinsetter roll was held: its pins, the ball it would be (position), how many are held
     * now, and why the first of them was rejected (status). */
    GAME_OUT_ROLL_HELD,
    /* More pinsetter rolls were lost: lost is the total so far, to a full queue or to no room
     * to hold them. */
    GAME_OUT_ROLLS_LOST
} GameOutputKind;

typedef struct {
    GameOutputKind kind;
    void *to;
    RequestSeq seq;
    GameStatus status;
    Score score;
    FrameEvent frame;
    Pins pins;
    RollNumber position;
    uint8_t held;
    uint16_t lost;
} GameOutput;

#define GAME_MAX_SUBSCRIBERS 2U

#define GAME_EVENTS_PER_MESSAGE ((2U * SCORER_MAX_EVENTS) + 1U)
#define GAME_OUTBOX_CAPACITY (1U + (GAME_MAX_SUBSCRIBERS * GAME_EVENTS_PER_MESSAGE))

typedef struct {
    GameOutput items[GAME_OUTBOX_CAPACITY];
    uint8_t count;
} GameOutbox;

typedef struct {
    Pins pins[SCORER_MAX_BALLS];
    uint8_t count;
    GameStatus first_refused_for;
} HeldRolls;

typedef struct {
    void *queues[GAME_MAX_SUBSCRIBERS];
    uint8_t count;
} Subscribers;

typedef struct {
    Scorer scorer;
    Subscribers subscribers;
    HeldRolls held;
    uint16_t lost_to_full_queue;
    uint16_t lost_to_full_held_list;
} GameActor;

void GameActor_Init(GameActor *self, ScorerVariant variant, CountRule rule);

/* Handles one message. The outbox is emptied first, then filled. */
void GameActor_Handle(GameActor *self, const GameMessage *message, GameOutbox *outbox);

#ifdef __cplusplus
}
#endif

#endif /* GAME_ACTOR_H */
