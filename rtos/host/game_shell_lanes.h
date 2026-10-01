#ifndef GAME_SHELL_LANES_H
#define GAME_SHELL_LANES_H

/* Private to the host: a lane's pinsetter, for the host's interrupt side, which has a file of its
 * own, game_shell_isr.c, so that its stack tripwire can be the interrupt's. */

#include "game_shell.h"
#include "pinsetter.h"

/* The pinsetter of a lane, which its interrupt feeds. Stops, as a fault, for a lane no game is
 * hosted at. */
Pinsetter *GameShell_PinsetterOfLane(GameShellLane lane);

#endif /* GAME_SHELL_LANES_H */
