#include "pinsetter_timer.h"

#include "device.h"

/* The CMSDK timer 0's registers, on its 25 MHz clock. */
#define PINSETTER_TIMER_BASE 0x40000000UL
#define PINSETTER_TIMER_CTRL (*(volatile uint32_t *)(PINSETTER_TIMER_BASE + 0x00U))
#define PINSETTER_TIMER_RELOAD (*(volatile uint32_t *)(PINSETTER_TIMER_BASE + 0x08U))
#define PINSETTER_TIMER_VALUE (*(volatile uint32_t *)(PINSETTER_TIMER_BASE + 0x04U))
#define PINSETTER_TIMER_INTCLEAR (*(volatile uint32_t *)(PINSETTER_TIMER_BASE + 0x0CU))
#define PINSETTER_TIMER_ENABLE 0x1U
#define PINSETTER_TIMER_INTERRUPT_ENABLE 0x8U
#define PINSETTER_TIMER_TICKS_PER_US (DEVICE_CPU_CLOCK_HZ / 1000000U)

static const PinsetterTimerRoll *s_script;
static uint8_t s_count;
static volatile uint8_t s_counted;

void PinsetterTimer_Start(const PinsetterTimerRoll *script, uint8_t count, uint32_t period_us)
{
    s_script = script;
    s_count = count;
    s_counted = 0U;
    NVIC_SetPriority(TIMER0_IRQn, DEVICE_PINSETTER_PRIORITY);
    NVIC_EnableIRQ(TIMER0_IRQn);
    PINSETTER_TIMER_RELOAD = (period_us * PINSETTER_TIMER_TICKS_PER_US) - 1U;
    PINSETTER_TIMER_VALUE = PINSETTER_TIMER_RELOAD;
    PINSETTER_TIMER_CTRL = PINSETTER_TIMER_ENABLE | PINSETTER_TIMER_INTERRUPT_ENABLE;
}

bool PinsetterTimer_IsDone(void)
{
    return s_counted == s_count;
}

void TIMER0_IRQHandler(void);

void TIMER0_IRQHandler(void)
{
    PINSETTER_TIMER_INTCLEAR = 1U;
    const uint8_t next = s_counted;
    if (next >= s_count) {
        PINSETTER_TIMER_CTRL = 0U;
        return;
    }
    s_counted = (uint8_t)(next + 1U);
    ActorHost_PinsetterCountedFromIsr(s_script[next].lane, s_script[next].pins);
}
