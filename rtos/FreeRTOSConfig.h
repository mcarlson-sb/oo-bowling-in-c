#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS, configured for the RTOS shell: the POSIX port on a Linux host, with every kernel
 * object allocated statically. There is no heap: configSUPPORT_DYNAMIC_ALLOCATION is 0, so only
 * the xTaskCreateStatic and xQueueCreateStatic families exist. */

#include "fault.h"

#define configUSE_PREEMPTION 1
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 5
/* In words. The POSIX port runs each task on a pthread whose stack it sizes from this, and a
 * pthread stack must be at least PTHREAD_STACK_MIN (16 KiB on glibc), so 2048 64-bit words. */
#define configMINIMAL_STACK_SIZE 2048U
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_64_BITS
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
#define INCLUDE_vTaskEndScheduler 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/* A broken kernel invariant stops the program, as every other fail-stop in the library does. */
#define configASSERT(condition)                        \
    do {                                               \
        if (!(condition)) {                            \
            Fault_Stop("FreeRTOS: configASSERT failed"); \
        }                                              \
    } while (0)

#endif /* FREERTOS_CONFIG_H */
