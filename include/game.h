#ifndef GAME_H
#define GAME_H

/* A bowling game. The representation is hidden: callers hold an opaque handle and act on
 * it only through these functions. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Game Game;

Game *Game_Create(void);
void Game_Destroy(Game *game);
uint16_t Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
