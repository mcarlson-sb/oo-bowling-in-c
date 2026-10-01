/* Fault_Stop, the fail-stop every "stops the program" in the library ends in. */
#include <gtest/gtest.h>

#include <csignal>

#include "fault.h"

namespace {

#ifdef _WIN32
/* How abort() ends a Windows process depends on the C runtime: MSVCRT exits with 3, and the
 * UCRT fails fast, with STATUS_STACK_BUFFER_OVERRUN (0xC0000409). */
bool ExitedThroughAbort(int exit_status)
{
    return (exit_status == 3) || (exit_status == static_cast<int>(0xC0000409U));
}
#endif

} // namespace

TEST(FaultDeathTest, should_print_the_reason_and_stop_with_abort)
{
    /* abort(), not exit(): on a target, the fault handler and a debugger see a crash, not a
     * clean shutdown. */
#ifdef _WIN32
    EXPECT_EXIT(Fault_Stop("the reason"), ExitedThroughAbort, "the reason");
#else
    EXPECT_EXIT(Fault_Stop("the reason"), ::testing::KilledBySignal(SIGABRT), "the reason");
#endif
}
