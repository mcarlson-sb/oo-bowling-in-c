#ifndef MESSAGE_H
#define MESSAGE_H

/* The protocol every actor speaks: one message type, an envelope and its selector's payload,
 * for requests, replies and events alike. Several kinds of actor answer the same selectors,
 * and which kind sits at an id is bound late, by the shell's routing table. */

#include <stdbool.h>
#include <stdint.h>

#include "actor_id.h"
#include "bowling_status.h"
#include "bowling_types.h"
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Chosen by the caller, and echoed in the reply, so it can have several requests in flight. */
typedef uint16_t RequestSeq;

typedef enum {
    /* A new game, by the rules it carries. Refused with GAME_ERR_INVALID_RULES, changing
     * nothing, if the scorer can't play them. */
    MSG_NEW_GAME,
    MSG_ROLL,
    /* from is the subscriber: it gets the reply, a catch-up of the complete frames, then every
     * frame change. */
    MSG_SUBSCRIBE,
    MSG_UNSUBSCRIBE, /* from is the subscriber, which gets the reply */
    /* An edit (see RollEdit), with its new balls carried in the message: a queue copies the
     * message, so nothing in it may point back into the sender's memory. A correction is an
     * edit of one ball out and one in. */
    MSG_EDIT,
    /* A roll the pinsetter counted, from its interrupt: from no one, and no reply. One the
     * game rejects is held, with every pinsetter roll after it, until a correction lets it
     * through; see GameActor. */
    MSG_PINSETTER_ROLL,
    MSG_QUERY_SCORE,
    /* Throws away the first held roll, a glitch, and lets the rest through as far as they go.
     * GAME_ERR_NO_SUCH_ROLL if nothing is held. */
    MSG_DISCARD_HELD,
    /* How many rolls have been lost so far: the pinsetter's to its full queue, told to the game;
     * the game's total, to a full queue or to no room to hold them, told to its subscribers. */
    MSG_ROLLS_LOST,
    /* To a request's from, with its seq. The score is after everything the request caused, for
     * a Roll, an Edit, an accepted DiscardHeld and a QueryScore; 0 in every other reply. */
    MSG_REPLY,
    MSG_FRAME_CHANGED,
    /* A pinsetter roll was held: its pins, the ball it would be (position), how many are held
     * now, and why the first of them was rejected (status). */
    MSG_ROLL_HELD,
    /* To from, with its seq: the selector sent was one the kind at "to" doesn't answer. Never
     * answered itself. */
    MSG_NOT_UNDERSTOOD,
    /* Every kind answers it, with MSG_STATS: its counters, and the facts behind its answers. */
    MSG_QUERY_STATS,
    /* To a MSG_QUERY_STATS's from, with its seq. A field a kind has nothing for is 0. */
    MSG_STATS,
    MSG_SELECTOR_COUNT
} Selector;

/* What a message asks, who from and who to: every message has one. A reply goes to from. */
typedef struct {
    Selector selector;
    ActorId from;
    ActorId to;
    RequestSeq seq;
} Envelope;

typedef struct {
    ScorerRules rules;
} NewGamePayload;

typedef struct {
    Pins pins;
} RollPayload;

typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    uint8_t new_count;
    Pins new_pins[SCORER_MAX_BALLS];
} EditPayload;

typedef struct {
    uint16_t lost;
} RollsLostPayload;

typedef struct {
    GameStatus status;
    Score score;
} ReplyPayload;

typedef struct {
    Pins pins;
    RollNumber position;
    uint8_t held;
    GameStatus status;
} RollHeldPayload;

typedef struct {
    Selector selector;
} NotUnderstoodPayload;

/* What a kind knows, as facts rather than rounded answers, so that a consumer can combine several
 * exactly: an observer's total and complete frames give an average over many lanes. */
typedef struct {
    uint16_t not_understood;
    uint16_t rolls_lost;    /* a game's, so far */
    uint8_t rolls_held;     /* a game's, now */
    uint8_t complete_frames;
    Score total;            /* of the complete frames */
} StatsPayload;

/* An envelope, and the fields of its selector only. */
typedef struct {
    Envelope envelope;
    union {
        NewGamePayload new_game;      /* MSG_NEW_GAME */
        RollPayload roll;             /* MSG_ROLL, MSG_PINSETTER_ROLL */
        EditPayload edit;             /* MSG_EDIT */
        RollsLostPayload rolls_lost;  /* MSG_ROLLS_LOST */
        ReplyPayload reply;           /* MSG_REPLY */
        FrameEvent frame;             /* MSG_FRAME_CHANGED */
        RollHeldPayload roll_held;    /* MSG_ROLL_HELD */
        NotUnderstoodPayload not_understood; /* MSG_NOT_UNDERSTOOD */
        StatsPayload stats;           /* MSG_STATS */
    } payload;
} Message;

/* A selector is an enum any sender can fill in: only one of the protocol's indexes a kind's
 * protocol table. */
static inline bool Selector_IsInProtocol(Selector selector)
{
    return (unsigned)selector < MSG_SELECTOR_COUNT;
}

#ifdef __cplusplus
}
#endif

#endif /* MESSAGE_H */
