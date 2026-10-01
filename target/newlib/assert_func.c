#include "fault.h"

/* newlib's assert() calls this on a failed assertion. Its own prints through stdio, which needs a
 * heap and system calls the target has neither of: this one stops the program, as every other
 * fail-stop does. Debug builds only; release builds compile assert() out. */
_Noreturn void __assert_func(const char *file, int line, const char *function,
                             const char *expression);

_Noreturn void __assert_func(const char *file, int line, const char *function,
                             const char *expression)
{
    (void)file;
    (void)line;
    (void)function;
    Fault_Stop(expression);
}
