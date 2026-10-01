/* The QEMU image's startup: its vector table, and the reset handler that readies RAM and the FPU
 * and calls main. Written for this project, with FreeRTOS's CORTEX_MPS2_QEMU_IAR_GCC demo
 * (build/gcc/startup_gcc.c, MIT) as the starting point. */

#include <stdint.h>
#include <string.h>

#include "device.h"
#include "fault.h"

/* From the linker script (mps2_an386.ld). */
extern uint32_t __stack_top;
extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int main(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);

/* The handlers another file defines, the port's and the image's own, or else Default_Handler. */
#define STARTUP_WEAK_HANDLER(name) void name(void) __attribute__((weak, alias("Default_Handler")))
STARTUP_WEAK_HANDLER(NMI_Handler);
STARTUP_WEAK_HANDLER(MemManage_Handler);
STARTUP_WEAK_HANDLER(BusFault_Handler);
STARTUP_WEAK_HANDLER(UsageFault_Handler);
STARTUP_WEAK_HANDLER(SVC_Handler);
STARTUP_WEAK_HANDLER(DebugMon_Handler);
STARTUP_WEAK_HANDLER(PendSV_Handler);
STARTUP_WEAK_HANDLER(SysTick_Handler);
STARTUP_WEAK_HANDLER(TIMER0_IRQHandler);
STARTUP_WEAK_HANDLER(TIMER1_IRQHandler);

#define STARTUP_EXTERNAL_INTERRUPTS 32U

/* FUNCTION POINTER EXEMPTION: the Cortex-M's vector table, the stack's top and then every
 * exception's handler, by address; the hardware reads it at reset and on each exception. */
struct VectorTable {
    uint32_t *initial_stack;
    void (*exceptions[15])(void);
    void (*interrupts[STARTUP_EXTERNAL_INTERRUPTS])(void);
};

__attribute__((section(".vectors"), used)) const struct VectorTable g_vector_table = {
    &__stack_top,
    {
        Reset_Handler, NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler,
        UsageFault_Handler, NULL, NULL, NULL, NULL, SVC_Handler, DebugMon_Handler, NULL,
        PendSV_Handler, SysTick_Handler,
    },
    {
        [TIMER0_IRQn] = TIMER0_IRQHandler,
        [TIMER1_IRQn] = TIMER1_IRQHandler,
    },
};

void Default_Handler(void)
{
    Fault_Stop("mps2-an386: an exception with no handler");
}

void HardFault_Handler(void)
{
    Fault_Stop("mps2-an386: a hard fault");
}

/* The C++ tests' static objects, which register each TEST: their constructors, in link order. */
static void Startup_RunConstructors(void)
{
    for (void (**constructor)(void) = __init_array_start; constructor < __init_array_end;
         constructor++) {
        (*constructor)();
    }
}

void Reset_Handler(void)
{
    /* Full access to the FPU, coprocessors 10 and 11, before any code can use it. */
    SCB->CPACR |= (0xFUL << 20U);
    __DSB();
    __ISB();
    memcpy(&__data_start, &__data_load,
           (size_t)((uintptr_t)&__data_end - (uintptr_t)&__data_start));
    memset(&__bss_start, 0, (size_t)((uintptr_t)&__bss_end - (uintptr_t)&__bss_start));
    Startup_RunConstructors();
    (void)main();
    Fault_Stop("mps2-an386: main returned");
}
