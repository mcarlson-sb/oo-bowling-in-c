#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS, configured for the actor host on a Cortex-M4F: the ARM_CM4F port, with every kernel
 * object allocated statically. There is no heap: configSUPPORT_DYNAMIC_ALLOCATION is 0, so only
 * the xTaskCreateStatic and xQueueCreateStatic families exist. What differs between the images,
 * the clock and the interrupt controller, comes from the image's device.h. */

#include "device.h"
#include "fault.h"

#define configCPU_CLOCK_HZ DEVICE_CPU_CLOCK_HZ
#define configUSE_PREEMPTION 1
/* 1 ms. SysTick's reload, DEVICE_CPU_CLOCK_HZ / 1000 - 1, must fit its 24 bits: checked below. */
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 5
/* In words: the idle task's, which runs no hook. */
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_TIMERS 0
#define configUSE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TRACE_FACILITY 0
#define configCHECK_FOR_STACK_OVERFLOW 0

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/* The interrupt controller's priorities, numbered as CMSIS numbers them: 0 is the most urgent,
 * and the device implements __NVIC_PRIO_BITS of each priority's eight. The kernel runs at the
 * least urgent. An interrupt that calls a FreeRTOS FromISR function must be no more urgent than
 * DEVICE_MAX_SYSCALL_PRIORITY, checked below for the pinsetter's. One more urgent is never masked
 * by the kernel, and must call nothing of it. */
#define configPRIO_BITS __NVIC_PRIO_BITS
#define configKERNEL_INTERRUPT_PRIORITY (DEVICE_LOWEST_PRIORITY << (8U - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (DEVICE_MAX_SYSCALL_PRIORITY << (8U - configPRIO_BITS))

/* The port's handlers, by the names CMSIS's vector tables give them. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

_Static_assert((DEVICE_CPU_CLOCK_HZ / configTICK_RATE_HZ) - 1U <= 0x00FFFFFFU,
               "SysTick's reload for the tick fits its 24 bits");
_Static_assert((DEVICE_MAX_SYSCALL_PRIORITY > 0U) &&
               (DEVICE_MAX_SYSCALL_PRIORITY < DEVICE_LOWEST_PRIORITY),
               "the kernel masks a range of priorities, below 0 and above its own");
_Static_assert(DEVICE_PINSETTER_PRIORITY >= DEVICE_MAX_SYSCALL_PRIORITY,
               "the pinsetter's interrupt calls FromISR functions, so the kernel must mask it");
_Static_assert(DEVICE_PINSETTER_PRIORITY <= DEVICE_LOWEST_PRIORITY,
               "the pinsetter's interrupt has a priority the device implements");

/* A broken kernel invariant stops the program, as every other fail-stop in the library does. */
#define configASSERT(condition)                        \
    do {                                               \
        if (!(condition)) {                            \
            Fault_Stop("FreeRTOS: configASSERT failed"); \
        }                                              \
    } while (0)

#endif /* FREERTOS_CONFIG_H */
