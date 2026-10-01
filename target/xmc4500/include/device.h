#ifndef DEVICE_H
#define DEVICE_H

/* The bike's main board's device, for the Cortex-M4F port: an Infineon XMC4500-F144x1024. Its
 * system clock is 120 MHz, from the PLL on a 12 MHz crystal, as Infineon's system_XMC4500.c sets
 * it up before main. */

#include "XMC4500.h"

#define DEVICE_CPU_CLOCK_HZ 120000000U

/* Interrupt priorities, 0 the most urgent of 2^__NVIC_PRIO_BITS: see FreeRTOSConfig.h. */
#define DEVICE_LOWEST_PRIORITY ((1U << __NVIC_PRIO_BITS) - 1U)
#define DEVICE_MAX_SYSCALL_PRIORITY 5U
/* The pinsetter's interrupt calls FromISR functions. Which input it is waits for the hardware
 * stage: its priority doesn't. */
#define DEVICE_PINSETTER_PRIORITY 6U

#endif /* DEVICE_H */
