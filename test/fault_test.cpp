/* Fault_Stop, the fail-stop every "stops the program" in the library ends in. */
#include <gtest/gtest.h>

#include <csignal>

#include "fault.h"

TEST(FaultDeathTest, should_print_the_reason_and_stop_with_abort)
{
    /* abort(), not exit(): on a target, the fault handler and a debugger see a crash, not a
     * clean shutdown. */
#ifdef _WIN32
    EXPECT_EXIT(Fault_Stop("the reason"), ::testing::ExitedWithCode(3), "the reason");
#else
    EXPECT_EXIT(Fault_Stop("the reason"), ::testing::KilledBySignal(SIGABRT), "the reason");
#endif
}
