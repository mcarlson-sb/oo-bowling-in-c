#ifndef GAME_H
#define GAME_H

/* A bowling game. The representation is hidden: callers hold an opaque handle and act on
 * it only through these functions. */

#include "bowling_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Game Game;

typedef enum {
    GAME_OK = 0,
    /* The tenth frame is complete, so the game is over. The roll is rejected and the game
     * is left unchanged. */
    GAME_ERR_GAME_OVER,
    /* More pins than are standing. The roll is rejected and the game is left unchanged. */
    GAME_ERR_INVALID_PINS,
    /* The game handle is NULL, for example because Game_Create ran out of games. */
    GAME_ERR_NULL_GAME
} GameStatus;

Game *Game_Create(void);
void Game_Destroy(Game *game);
GameStatus Game_Roll(Game *game, Pins pins);
Score Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
