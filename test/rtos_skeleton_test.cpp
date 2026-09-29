/* The RTOS shell's walking skeleton: FreeRTOS on its POSIX port, statically allocated, runs a
 * task and stops. Each test starts the scheduler, so each runs in a process of its own (ctest
 * runs them one by one); a test here never sleeps to wait for another thread. */

#include <gtest/gtest.h>

#include <atomic>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace {

StaticTask_t s_task;
StackType_t s_stack[configMINIMAL_STACK_SIZE];
/* Written by the task, on its own pthread, and read here once the scheduler has stopped. The
 * POSIX port hands over with signals, which ThreadSanitizer can't see order anything, so the
 * result crosses as an atomic: with a plain bool, TSan reports a race in this file. */
std::atomic<bool> s_ran{false};

void SkeletonTask(void *parameter)
{
    (void)parameter;
    s_ran.store(true);
    vTaskEndScheduler(); /* back to vTaskStartScheduler's caller */
    for (;;) {
    }
}

} // namespace

TEST(RtosSkeletonTest, should_run_a_static_task_and_stop_the_scheduler)
{
    ASSERT_NE(nullptr, xTaskCreateStatic(&SkeletonTask, "skeleton", configMINIMAL_STACK_SIZE,
                                         nullptr, tskIDLE_PRIORITY + 1U, s_stack, &s_task));
    vTaskStartScheduler();
    EXPECT_TRUE(s_ran.load());
}
