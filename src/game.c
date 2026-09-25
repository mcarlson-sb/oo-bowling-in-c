#include "game.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "frame_context.h"

/* Frames in a game of bowling. The last one keeps its own fill balls, so a game never needs
 * more. */
#define GAME_FRAMES 10U

/* Games available at once. There is no heap, so games come from a fixed pool. */
#define GAME_POOL_SIZE 2U

struct Game {
    FrameContext frames[GAME_FRAMES];
    uint8_t frame_count;
    bool in_use;
};

static Game s_pool[GAME_POOL_SIZE];

static bool Game_IsOver(const Game *game)
{
    return (game->frame_count == GAME_FRAMES) &&
           FrameContext_IsComplete(&game->frames[GAME_FRAMES - 1U]);
}

static bool Game_HasNoFrames(const Game *game)
{
    return game->frame_count == 0U;
}

/* Only the latest frame can still be taking its own rolls, so only it can leave fewer than
 * ten pins standing. */
static Pins Game_PinsStanding(const Game *game)
{
    if (Game_HasNoFrames(game)) {
        return FRAME_ALL_PINS;
    }
    return FrameContext_PinsStanding(&game->frames[game->frame_count - 1U]);
}

static void Game_AddNewFrame(Game *game, Pins pins)
{
    FrameContext *new_frame = &game->frames[game->frame_count];
    if (game->frame_count == (GAME_FRAMES - 1U)) {
        FrameContext_InitTenth(new_frame);
    } else {
        FrameContext_Init(new_frame);
    }
    const RollResult result = FrameContext_Roll(new_frame, pins);
    assert(result.consumed); /* a new frame always keeps its first roll */
    (void)result;            /* used only by the assert, which NDEBUG removes */
    game->frame_count++;
}

/* Offers the roll to each frame, oldest first, until one keeps it (Chain of Responsibility).
 *
 * Only the latest frame can keep a roll: every earlier frame is complete or collecting
 * bonus rolls, and both pass the roll on. So in practice the roll is kept, if at all, on
 * the last pass through the loop, and the guard below is never false. Coverage reports it
 * as a branch never taken; that is expected. It stays as a defense: if a future state
 * broke that rule, the guard would stop a kept roll from also reaching later frames and
 * being counted twice. */
static RollResult Game_ApplyPinsToFrames(Game *game, Pins pins)
{
    RollResult result = RollResult_Passed(pins);
    for (uint8_t i = 0U; i < game->frame_count; i++) {
        if (!result.consumed) {
            result = FrameContext_Roll(&game->frames[i], result.pins);
        }
    }
    return result;
}

Game *Game_Create(void)
{
    for (uint8_t i = 0U; i < GAME_POOL_SIZE; i++) {
        Game *game = &s_pool[i];
        if (!game->in_use) {
            game->in_use = true;
            game->frame_count = 0U;
            return game;
        }
    }
    return NULL;
}

void Game_Destroy(Game *game)
{
    if (game == NULL) {
        return;
    }
    game->in_use = false;
}

GameStatus Game_Roll(Game *game, Pins pins)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    /* Both checks run before any frame sees the roll. Frames act on a roll as it passes
     * through them, and that can't be undone, so this is what leaves a rejected roll with
     * no effect. */
    if (Game_IsOver(game)) {
        return GAME_ERR_GAME_OVER;
    }
    if (pins > Game_PinsStanding(game)) {
        return GAME_ERR_INVALID_PINS;
    }

    const RollResult result = Game_ApplyPinsToFrames(game, pins);
    if (!result.consumed) {
        Game_AddNewFrame(game, result.pins);
    }
    return GAME_OK;
}

Score Game_Score(const Game *game)
{
    if (game == NULL) {
        return 0U;
    }

    Score score = 0U;
    for (uint8_t i = 0U; i < game->frame_count; i++) {
        score = (Score)(score + FrameContext_Score(&game->frames[i]));
    }
    return score;
}
