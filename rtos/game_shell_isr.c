#include "game_shell.h"
#include "game_shell_ports.h"

static uint16_t s_lost_to_full_queue; /* the interrupt's own: nothing else touches it */

void GameShell_ResetIsr(void)
{
    s_lost_to_full_queue = 0U;
}

void GameShell_PinsetterCountedFromIsr(Pins pins)
{
    const GameShellPorts *ports = GameShell_Ports();
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(ports->pinsetter, &pins, &woken) != pdPASS) {
        s_lost_to_full_queue++; /* the newest roll is the one dropped */
        (void)xQueueOverwriteFromISR(ports->lost_report, &s_lost_to_full_queue, &woken);
    }
    vTaskNotifyGiveFromISR(ports->game_task, &woken);
    portYIELD_FROM_ISR(woken);
}
