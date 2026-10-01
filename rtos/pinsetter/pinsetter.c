#include "pinsetter.h"

void Pinsetter_Start(Pinsetter *self, ActorId game, TaskHandle_t task)
{
    self->game = game;
    self->task = task;
    self->rolls = xQueueCreateStatic(PINSETTER_ROLLS, sizeof(Pins), self->rolls_storage,
                                     &self->rolls_queue);
    self->lost_report = xQueueCreateStatic(1U, sizeof(uint16_t), self->lost_report_storage,
                                           &self->lost_report_queue);
    self->lost_to_full_queue = 0U;
}

/* The pinsetter has no id, so no one can reply to it. */
static Envelope Pinsetter_AnonymousEventToItsGame(const Pinsetter *self, Selector selector)
{
    return Envelope_Event(selector, ACTOR_ID_NONE, self->game);
}

static bool Pinsetter_TakeRoll(Pinsetter *self, Message *message)
{
    Pins pins;
    if (xQueueReceive(self->rolls, &pins, 0U) != pdPASS) {
        return false;
    }
    message->envelope = Pinsetter_AnonymousEventToItsGame(self, MSG_PINSETTER_ROLL);
    message->payload.roll.pins = pins;
    return true;
}

static bool Pinsetter_TakeLostReport(Pinsetter *self, Message *message)
{
    uint16_t lost;
    if (xQueueReceive(self->lost_report, &lost, 0U) != pdPASS) {
        return false;
    }
    message->envelope = Pinsetter_AnonymousEventToItsGame(self, MSG_ROLLS_LOST);
    message->payload.rolls_lost.lost = lost;
    return true;
}

bool Pinsetter_Take(Pinsetter *self, Message *message)
{
    return Pinsetter_TakeRoll(self, message) || Pinsetter_TakeLostReport(self, message);
}
