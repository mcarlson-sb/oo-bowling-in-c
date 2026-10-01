#include "router.h"

static const Route s_no_route = { ACTOR_KIND_NONE, 0U, NULL, NULL };

void Router_Reset(Router *self)
{
    for (uint8_t id = 0U; id < ROUTER_IDS; id++) {
        self->routes[id] = s_no_route;
    }
    atomic_init(&self->dropped, 0U);
}

void Router_Bind(Router *self, ActorId id, Route route)
{
    self->routes[id] = route;
}

const Route *Router_RouteTo(const Router *self, ActorId id)
{
    return (id < ROUTER_IDS) ? &self->routes[id] : &s_no_route;
}

bool Router_Post(const Router *self, const Message *message, TickType_t wait)
{
    const Route *route = Router_RouteTo(self, message->envelope.to);
    if ((route->mailbox == NULL) || (xQueueSend(route->mailbox, message, wait) != pdPASS)) {
        return false;
    }
    if (route->task != NULL) {
        xTaskNotifyGive(route->task);
    }
    return true;
}

void Router_Deliver(Router *self, const Outbox *outbox)
{
    for (uint8_t i = 0U; i < outbox->count; i++) {
        if (!Router_Post(self, &outbox->items[i], 0U)) {
            Router_CountDropped(self);
        }
    }
}

void Router_CountDropped(Router *self)
{
    (void)atomic_fetch_add_explicit(&self->dropped, 1U, memory_order_relaxed);
}

uint16_t Router_Dropped(const Router *self)
{
    return (uint16_t)atomic_load_explicit(&self->dropped, memory_order_relaxed);
}
