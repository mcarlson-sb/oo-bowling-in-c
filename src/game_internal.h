#ifndef GAME_INTERNAL_H
#define GAME_INTERNAL_H

/* What the library's own modules may ask a game, and callers may not. Private: in src/, so
 * nothing outside the library can include it. */

#include <stdbool.h>

#include "game.h"

/* True while the game is telling its listeners about a change, so a call made now comes from
 * inside a listener. False for NULL. */
bool Game_IsNotifying(const Game *game);

#endif /* GAME_INTERNAL_H */
