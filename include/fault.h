#ifndef FAULT_H
#define FAULT_H

/* Fail-stop, in every build, for an error with no safe way on. The host version prints
 * `reason` and aborts; a target replaces it by defining its own Fault_Stop, which must not
 * return. */

#ifdef __cplusplus
extern "C" {
[[noreturn]] void Fault_Stop(const char *reason);
}
#else
_Noreturn void Fault_Stop(const char *reason);
#endif

#endif /* FAULT_H */
