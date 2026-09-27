#ifndef FRAME_LISTENERS_H
#define FRAME_LISTENERS_H

/* Who a game tells about changed frames: a fixed list of (callback, context) pairs. It keeps
 * and tells them, and nothing more: whether the game is in the middle of something is the
 * game's to know (see Game's `busy`). A value type, held inside Game with no heap; it holds
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
} FrameListeners;

/* No listeners. */
void FrameListeners_Init(FrameListeners *self);

/* Adds a listener. Returns false, adding nothing, if the list is full or `callback` is NULL. */
bool FrameListeners_Add(FrameListeners *self, FrameChangedCallback callback, void *context);

/* Tells every listener, in the order they were added, about one frame.
 *
 * Inline: as a call into another file it added its own
 * frame, 80 bytes on a 64-bit host, under every listener callback, and made the caller's
 * frame bigger too. Inlined, the stack is what it was before the listeners had a module. */
static inline void FrameListeners_Tell(FrameListeners *self, FrameNumber frame_number,
                                       Score frame_score, bool frame_complete)
{
    for (uint8_t i = 0U; i < self->count; i++) {
        const FrameListener *listener = &self->entries[i];
        listener->callback(listener->context, frame_number, frame_score, frame_complete);
    }
}

/* Tells only the newest listener, the last one added, about one frame: to catch it up on a
 * game already under way. Inline, like FrameListeners_Tell, for the same reason. */
static inline void FrameListeners_TellNewest(const FrameListeners *self, FrameNumber frame_number,
                                             Score frame_score, bool frame_complete)
{
    if (self->count == 0U) {
        return;
    }
    const FrameListener *newest = &self->entries[self->count - 1U];
    newest->callback(newest->context, frame_number, frame_score, frame_complete);
}

#endif /* FRAME_LISTENERS_H */
