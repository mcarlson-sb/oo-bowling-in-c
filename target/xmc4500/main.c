/* The firmware for the bike's main board, the XMC4500: Infineon's startup sets the clock to
 * 120 MHz (SystemInit), readies RAM, and calls this. It hosts the lanes and starts the scheduler,
 * which never returns.
 *
 * Left for the hardware stage: the pinsetters' interrupt, which input it is, and its handler,
 * which calls ActorHost_PinsetterCountedFromIsr at DEVICE_PINSETTER_PRIORITY; and the link to
 * whoever runs the league, which sends each lane its NEW_GAME and subscribes its scoreboard. */

#include "FreeRTOS.h"
#include "task.h"

#include "fault.h"
#include "firmware.h"

int main(void)
{
    Firmware_HostTheLanes();
    vTaskStartScheduler();
    Fault_Stop("XMC4500: the scheduler returned");
}
