/* Compiled by the actor_state_is_hidden test with include/ alone on its path, and must fail to:
 * outside the library, the shell and the tests, an actor's state can't even be allocated. */

#include "game_actor.h"
#include "running_average.h"
#include "scoreboard.h"

GameActor game;
Scoreboard board;
RunningAverage average;
