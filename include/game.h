#ifndef GAME_H
#define GAME_H

/* A bowling game. The representation is hidden: callers hold an opaque handle and act on
 * it only through these functions. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Game Game;

typedef enum {
    GAME_OK = 0,
    /* The tenth frame is complete, so the game is over. The roll is rejected and the game
     * is left unchanged. */
    GAME_ERR_GAME_OVER
} GameStatus;

Game *Game_Create(void);
void Game_Destroy(Game *game);
GameStatus Game_Roll(Game *game, uint8_t pins);
uint16_t Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
