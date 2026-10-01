#ifndef MPS2_AN386_H
#define MPS2_AN386_H

/* The CMSIS device header for ARM's MPS2 board with the AN386 image, a Cortex-M4F, as QEMU's
 * mps2-an386 machine models it: the interrupts this code uses, and the core's configuration,
 * from the AN386 application note. QEMU's model is the stand-in for the XMC4500 in CI. */

typedef enum {
    NonMaskableInt_IRQn = -14,
    HardFault_IRQn = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn = -11,
    UsageFault_IRQn = -10,
    SVCall_IRQn = -5,
    DebugMonitor_IRQn = -4,
    PendSV_IRQn = -2,
    SysTick_IRQn = -1,
    TIMER0_IRQn = 8, /* the CMSDK timer at 0x40000000 */
    TIMER1_IRQn = 9
} IRQn_Type;

#define __CM4_REV 0x0001U
#define __MPU_PRESENT 1
#define __NVIC_PRIO_BITS 3
#define __Vendor_SysTickConfig 0
#define __FPU_PRESENT 1

#include "core_cm4.h"

#endif /* MPS2_AN386_H */
