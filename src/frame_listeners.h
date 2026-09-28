#ifndef FRAME_LISTENERS_H
#define FRAME_LISTENERS_H

/* Who a game tells about changed frames. It keeps and tells them, and nothing more. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "bowling_types.h"
#include "game.h"

#define FRAME_LISTENERS_MAX 2U

typedef struct {
    FrameChangedCallback callback;
    void *context;
} FrameListener;

typedef struct {
    FrameListener entries[FRAME_LISTENERS_MAX];
    uint8_t count;
} FrameListeners;

void FrameListeners_Init(FrameListeners *self);

/* False, adding nothing, if the list is full or `callback` is NULL. */
bool FrameListeners_Add(FrameListeners *self, FrameChangedCallback callback, void *context);

/* In the order they were added. Inline, like TellNewest, because every callback's stack sits
 * on top of it. */
static inline void FrameListeners_Tell(FrameListeners *self, FrameNumber frame_number,
                                       Score frame_score, bool frame_complete)
{
    for (uint8_t i = 0U; i < self->count; i++) {
        const FrameListener *listener = &self->entries[i];
        listener->callback(listener->context, frame_number, frame_score, frame_complete);
    }
}

static inline void FrameListeners_TellNewest(const FrameListeners *self, FrameNumber frame_number,
                                             Score frame_score, bool frame_complete)
{
    assert(self->count > 0U);
    if (self->count == 0U) {
        return;
    }
    const FrameListener *newest = &self->entries[self->count - 1U];
    newest->callback(newest->context, frame_number, frame_score, frame_complete);
}

#endif /* FRAME_LISTENERS_H */
