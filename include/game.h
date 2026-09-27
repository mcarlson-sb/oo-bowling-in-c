#ifndef GAME_H
#define GAME_H

/* A bowling game. The representation is hidden: callers hold an opaque handle and act on
 * it only through these functions. */

#include <stdbool.h>

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
    GAME_ERR_NULL_GAME,
    /* The game's PinCountRule counted the roll as more pins than were standing. The roll
     * is rejected and the game is left unchanged. */
    GAME_ERR_RULE_OUT_OF_RANGE
} GameStatus;

/* How many pins a roll counts as, given how many were standing before it and how many it
 * knocked down. A caller supplies one to play a variant of the game; standard bowling
 * counts exactly the pins that fell. It must return no more than pins_standing; Game_Roll
 * rejects a roll whose count is out of range (GAME_ERR_RULE_OUT_OF_RANGE). */
typedef Pins (*PinCountRule)(Pins pins_standing, Pins pins_down);

/* A standard game. */
Game *Game_Create(void);

/* A game whose rolls are counted by `count_pins`. NULL if `count_pins` is NULL, or if no
 * game is free. */
Game *Game_CreateWithRule(PinCountRule count_pins);

void Game_Destroy(Game *game);

/* Told each time a frame completes, after the roll that completed it: the frame's number (1
 * to 10) and its score. When one roll completes several frames, it is told about each,
 * oldest first. */
typedef void (*FrameCompletedCallback)(void *context, uint8_t frame_number, Score frame_score);

/* Adds a callback this game tells about completed frames; `context` is passed back to it
 * unchanged. A game has room for two. Returns false, adding nothing, if both are taken or
 * `game` is NULL. */
bool Game_OnFrameCompleted(Game *game, FrameCompletedCallback callback, void *context);

GameStatus Game_Roll(Game *game, Pins pins);
Score Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
