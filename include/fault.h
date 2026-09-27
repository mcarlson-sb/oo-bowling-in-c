#ifndef FAULT_H
#define FAULT_H

/* Fail-stop: what the library does when it finds an error there's no safe way to go on from,
 * such as a configuration too small for the system it was built into. Unlike an assert, a
 * fault stays in a release build.
 *
 * The library brings a host version, in src/fault.c: it writes `reason` to stderr and calls
 * abort(). A target build replaces it at link time by defining Fault_Stop itself, for example
 * to log the reason and wait for the watchdog. The host version is alone in its own file, so
 * a definition in the application keeps the library's out of the link. Whatever it does, it
 * must not return. */

#ifdef __cplusplus
extern "C" {
[[noreturn]] void Fault_Stop(const char *reason);
}
#else
_Noreturn void Fault_Stop(const char *reason);
#endif

#endif /* FAULT_H */
