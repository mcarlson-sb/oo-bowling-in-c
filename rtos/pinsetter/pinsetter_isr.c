#include "pinsetter.h"

void Pinsetter_CountedFromIsr(Pinsetter *self, Pins pins)
{
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(self->rolls, &pins, &woken) != pdPASS) {
        self->lost_to_full_queue++;
        (void)xQueueOverwriteFromISR(self->lost_report, &self->lost_to_full_queue, &woken);
    }
    vTaskNotifyGiveFromISR(self->task, &woken);
    portYIELD_FROM_ISR(woken);
}
