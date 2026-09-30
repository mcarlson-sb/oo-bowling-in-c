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

/* What one message sent, in storage its owner provides: the task that hosts the actor, sized for
 * the largest burst of the kinds it hosts. */
typedef struct {
    Message *items;
    uint8_t capacity;
    uint8_t count;
} Outbox;

void Outbox_Init(Outbox *self, Message *storage, uint8_t capacity);

Message *Outbox_Next(Outbox *self, Envelope envelope);

/* A reply to `request`: from the id it was sent to, to its sender, with its seq. */
Message *Outbox_BeginReply(Outbox *self, const Message *request);

void Outbox_FinishReply(Message *reply, GameStatus status, Score score);

/* A reply whose outcome is known at once. */
void Outbox_Reply(Outbox *self, const Message *request, GameStatus status, Score score);

/* A MSG_STATS answer to `request`, as a reply is addressed, with every field 0 for the kind to
 * fill in what it knows. */
StatsPayload *Outbox_BeginStats(Outbox *self, const Message *request);

/* Answers that the request's selector isn't one its kind responds to, from `from`, the kind's own
 * id, if the request wants to hear it (see Envelope_WantsNotUnderstood). */
void Outbox_NotUnderstood(Outbox *self, ActorId from, const Message *request);

#ifdef __cplusplus
}
#endif

#endif /* OUTBOX_H */
