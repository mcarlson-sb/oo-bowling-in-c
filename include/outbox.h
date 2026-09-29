#ifndef OUTBOX_H
#define OUTBOX_H

/* What one message makes an actor send, and the ways every kind answers a request: a reply, and
 * a NOT_UNDERSTOOD for a selector it doesn't respond to. */

#include <stdbool.h>
#include <stdint.h>

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most one message makes any actor send: the game's worst case (game_actor.c asserts it). */
#define OUTBOX_CAPACITY 43U

typedef struct {
    Message items[OUTBOX_CAPACITY];
    uint8_t count;
} Outbox;

/* The next message out, from `from` to `to`, with no seq. */
Message *Outbox_Next(Outbox *self, Selector selector, ActorId from, ActorId to);

/* A reply to `request`: from the id it was sent to, to its sender, with its seq. */
Message *Outbox_BeginReply(Outbox *self, const Message *request);

void Outbox_FinishReply(Message *reply, GameStatus status, Score score);

/* A reply whose outcome is known at once. */
void Outbox_Reply(Outbox *self, const Message *request, GameStatus status, Score score);

/* The request's selector isn't one its kind responds to. Answered, from `from`, unless it came
 * from no one, who can't hear it, or is a NOT_UNDERSTOOD itself, which two kinds would echo. */
void Outbox_NotUnderstood(Outbox *self, ActorId from, const Message *request);

#ifdef __cplusplus
}
#endif

#endif /* OUTBOX_H */
