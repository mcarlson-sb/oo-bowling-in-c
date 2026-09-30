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

static bool Pinsetter_TakeRoll(Pinsetter *self, Message *message)
{
    Pins pins;
    if (xQueueReceive(self->rolls, &pins, 0U) != pdPASS) {
        return false;
    }
    /* From no one: the pinsetter has no id, and hears no reply. */
    message->envelope = Envelope_Event(MSG_PINSETTER_ROLL, ACTOR_ID_NONE, self->game);
    message->payload.roll.pins = pins;
    return true;
}

static bool Pinsetter_TakeLostReport(Pinsetter *self, Message *message)
{
    uint16_t lost;
    if (xQueueReceive(self->lost_report, &lost, 0U) != pdPASS) {
        return false;
    }
    message->envelope = Envelope_Event(MSG_ROLLS_LOST, ACTOR_ID_NONE, self->game);
    message->payload.rolls_lost.lost = lost;
    return true;
}

bool Pinsetter_Take(Pinsetter *self, TaskHandle_t task, Message *message)
{
    if (task != self->task) {
        return false;
    }
    return Pinsetter_TakeRoll(self, message) || Pinsetter_TakeLostReport(self, message);
}
