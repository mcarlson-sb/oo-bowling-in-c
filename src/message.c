#include "message.h"

_Static_assert(GAME_OK == REPLY_OK, "a game's OK is the protocol's REPLY_OK");

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

/* The answers: to a request, or to no one's in particular. Every other selector is a request. */
static const bool k_is_an_answer[MSG_SELECTOR_COUNT] = {
    [MSG_REPLY] = true,
    [MSG_FRAME_CHANGED] = true,
    [MSG_ROLL_HELD] = true,
    [MSG_ROLLS_LOST] = true,
    [MSG_NOT_UNDERSTOOD] = true,
    [MSG_STATS] = true,
};

bool Selector_IsARequest(Selector selector)
{
    if (!Selector_IsInProtocol(selector)) {
        return true; /* a sender asked for something: that it isn't understood is the answer */
    }
    return !k_is_an_answer[selector];
}

bool Envelope_WantsAnAnswer(const Envelope *message)
{
    return (message->from != ACTOR_ID_NONE) && Selector_IsARequest(message->selector);
}
