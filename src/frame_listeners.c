#include "frame_listeners.h"

#include <stddef.h>

void FrameListeners_Init(FrameListeners *self)
{
    self->count = 0U;
    self->telling = false;
}

bool FrameListeners_Add(FrameListeners *self, FrameChangedCallback callback, void *context)
{
    if ((callback == NULL) || (self->count == FRAME_LISTENERS_MAX)) {
        return false;
    }
    FrameListener *listener = &self->entries[self->count];
    listener->callback = callback;
    listener->context = context;
    self->count++;
    return true;
}
