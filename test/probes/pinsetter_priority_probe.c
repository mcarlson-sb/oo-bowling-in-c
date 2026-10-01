/* Must not compile: the pinsetter's interrupt one level more urgent than the kernel masks, which
 * would let its FromISR calls run inside the kernel's critical sections. FreeRTOSConfig.h's check
 * refuses it, on every image. */
#include "device.h"

#undef DEVICE_PINSETTER_PRIORITY
#define DEVICE_PINSETTER_PRIORITY (DEVICE_MAX_SYSCALL_PRIORITY - 1U)

#include "FreeRTOSConfig.h"
