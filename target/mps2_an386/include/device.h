#ifndef DEVICE_H
#define DEVICE_H

/* The QEMU image's device, for the Cortex-M4F port: mps2-an386, whose system clock, and SysTick's,
 * is 25 MHz. */

#include "mps2_an386.h"

#define DEVICE_CPU_CLOCK_HZ 25000000U

/* Interrupt priorities, 0 the most urgent of 2^__NVIC_PRIO_BITS: see FreeRTOSConfig.h. */
#define DEVICE_LOWEST_PRIORITY ((1U << __NVIC_PRIO_BITS) - 1U)
#define DEVICE_MAX_SYSCALL_PRIORITY 2U
/* The pinsetter's interrupt, a CMSDK timer standing in for it, calls FromISR functions. */
#define DEVICE_PINSETTER_PRIORITY 3U

#endif /* DEVICE_H */
