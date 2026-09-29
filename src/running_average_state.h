#ifndef RUNNING_AVERAGE_STATE_H
#define RUNNING_AVERAGE_STATE_H

/* The running average's state: private to the library, the shell that hosts it and the tests. */

#include <stdint.h>

#include "frame_board.h"
#include "running_average.h"

#ifdef __cplusplus
extern "C" {
#endif

struct RunningAverage {
    ActorId id;
    FrameBoard board;
    uint16_t not_understood;
};

#ifdef __cplusplus
}
#endif

#endif /* RUNNING_AVERAGE_STATE_H */
