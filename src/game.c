#include "game.h"

#include <stdbool.h>

#include "frame_context.h"

/* Enough frames for a full game: ten, plus the two extra frames that tenth-frame bonus
 * rolls open in this design. */
#define GAME_MAX_FRAMES 12U

struct Game {
    FrameContext frames[GAME_MAX_FRAMES];
    uint8_t frame_count;
};

static Game s_game;

static bool Game_IsFirstFrame(const Game *game)
{
    return game->frame_count == 0U;
}

static void Game_AddNewFrame(Game *game, uint8_t pins)
{
    FrameContext *new_frame = &game->frames[game->frame_count];
    FrameContext_Init(new_frame);
    (void)FrameContext_Roll(new_frame, pins);
    game->frame_count++;
}

static int16_t Game_ApplyPinsToFrames(Game *game, uint8_t pins)
{
    int16_t remaining = (int16_t)pins;
    for (uint8_t i = 0U; i < game->frame_count; i++) {
        if (remaining != FRAME_ROLL_CONSUMED) {
            remaining = FrameContext_Roll(&game->frames[i], (uint8_t)remaining);
        }
    }
    return remaining;
}

static int16_t Game_UpdateFrames(Game *game, uint8_t pins)
{
    if (Game_IsFirstFrame(game)) {
        Game_AddNewFrame(game, pins);
        return FRAME_ROLL_CONSUMED;
    }
    return Game_ApplyPinsToFrames(game, pins);
}

Game *Game_Create(void)
{
    s_game.frame_count = 0U;
    return &s_game;
}

void Game_Destroy(Game *game)
{
    (void)game;
}

void Game_Roll(Game *game, uint8_t pins)
{
    const int16_t remaining_pins = Game_UpdateFrames(game, pins);
    if (remaining_pins != FRAME_ROLL_CONSUMED) {
        Game_AddNewFrame(game, (uint8_t)remaining_pins);
    }
}

uint16_t Game_Score(const Game *game)
{
    uint16_t score = 0U;
    for (uint8_t i = 0U; i < game->frame_count; i++) {
        score = (uint16_t)(score + FrameContext_Score(&game->frames[i]));
    }
    return score;
}
