#define _GNU_SOURCE /* pthread_getattr_np */

#include "port_stack.h"

#include <pthread.h>
#include <stddef.h>

uintptr_t PortStack_Lowest(void)
{
    pthread_attr_t attributes;
    void *lowest = NULL;
    size_t size = 0U;
    (void)pthread_getattr_np(pthread_self(), &attributes);
    (void)pthread_attr_getstack(&attributes, &lowest, &size);
    (void)pthread_attr_destroy(&attributes);
    return (uintptr_t)lowest;
}
