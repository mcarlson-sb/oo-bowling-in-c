#ifndef SCOREBOARD_STATE_H
#define SCOREBOARD_STATE_H

/* The scoreboard's state: private to the library, the shell that hosts it and the tests. */

#include <stdint.h>

#include "frame_board.h"
#include "scoreboard.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Scoreboard {
    FrameBoard board;
    uint16_t not_understood;
};

#ifdef __cplusplus
}
#endif

#endif /* SCOREBOARD_STATE_H */
