#include "game.h"

struct Game {
    int unused;
};

static Game s_game;

Game *Game_Create(void)
{
    return &s_game;
}

void Game_Destroy(Game *game)
{
    (void)game;
}
