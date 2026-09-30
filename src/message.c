#include "message.h"

Envelope Envelope_Event(Selector selector, ActorId from, ActorId to)
{
    const Envelope event = { selector, from, to, 0U };
    return event;
}

Envelope Envelope_ReplyTo(const Envelope *request, Selector selector)
{
    const Envelope reply = { selector, request->to, request->from, request->seq };
    return reply;
}

bool Envelope_WantsNotUnderstood(const Envelope *request)
{
    return (request->from != ACTOR_ID_NONE) && (request->selector != MSG_NOT_UNDERSTOOD);
}
