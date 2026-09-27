#ifndef FRAME_LISTENERS_H
#define FRAME_LISTENERS_H

/* Who a game tells about changed frames, and whether it is telling them right now: a fixed
 * list of (callback, context) pairs. A value type, held inside Game with no heap; it holds
 * no pointer into itself, so it could be copied, though nothing needs to.
 *
 * Private to the library: lives in src/, not include/. */

#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "game.h"

/* Listeners one game can have. */
#define FRAME_LISTENERS_MAX 2U

typedef struct {
    FrameChangedCallback callback;
    void *context;
} FrameListener;

typedef struct {
    FrameListener entries[FRAME_LISTENERS_MAX];
    uint8_t count;
    bool telling; /* true while a callback runs: see FrameListeners_AreBeingTold */
} FrameListeners;

/* No listeners, and none being told. */
void FrameListeners_Init(FrameListeners *self);

/* Adds a listener. Returns false, adding nothing, if the list is full or `callback` is NULL. */
bool FrameListeners_Add(FrameListeners *self, FrameChangedCallback callback, void *context);

/* Tells every listener, in the order they were added, about one frame.
 *
 * Inline, like FrameListeners_AreBeingTold: as a call into another file it added its own
 * frame, 80 bytes on a 64-bit host, under every listener callback, and made the caller's
 * frame bigger too. Inlined, the stack is what it was before the listeners had a module. */
static inline void FrameListeners_Tell(FrameListeners *self, FrameNumber frame_number,
                                       Score frame_score, bool frame_complete)
{
    self->telling = true;
    for (uint8_t i = 0U; i < self->count; i++) {
        const FrameListener *listener = &self->entries[i];
        listener->callback(listener->context, frame_number, frame_score, frame_complete);
    }
    self->telling = false;
}

/* True while a listener is being told, so a call made now comes from inside a callback. */
static inline bool FrameListeners_AreBeingTold(const FrameListeners *self)
{
    return self->telling;
}

#endif /* FRAME_LISTENERS_H */
