#ifndef GAME_H
#define GAME_H

/* A game of ten-pin bowling, behind an opaque handle. Every error status leaves the game
 * unchanged. */

#include <stdbool.h>

#include "bowling_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Game Game;

typedef enum {
    GAME_OK = 0,
    GAME_ERR_GAME_OVER,         /* the tenth frame is complete */
    GAME_ERR_INVALID_PINS,      /* more pins than are standing */
    GAME_ERR_NULL_GAME,
    GAME_ERR_RULE_OUT_OF_RANGE, /* the PinCountRule counted more pins than were standing */
    /* Called from inside a frame-changed callback or the PinCountRule, while a roll or an
     * edit is in progress; or Pinsetter_Drain called from inside a drain. */
    GAME_ERR_BUSY,
    GAME_ERR_NO_SUCH_ROLL,      /* an edit's range isn't rolls the game has had */
    GAME_ERR_TOO_MANY_ROLLS     /* an edit would leave more than 21 rolls */
} GameStatus;

/* How many pins a roll counts as, given the pins standing and the pins that fell. Supplied to
 * play a variant; the standard rule counts the pins that fell.
 *   - It returns at most pins_standing, or the roll gets GAME_ERR_RULE_OUT_OF_RANGE.
 *   - It is pure: every edit replays every roll through it. If a rejected edit can't be undone
 *     because the rule now answers differently, the program stops (Fault_Stop).
 * It runs on up to 432 bytes of the library's stack at -O2, where inlining makes the edit's frame
 * large, and 368 at -O0 (64-bit host; measure on the target). */
typedef Pins (*PinCountRule)(Pins pins_standing, Pins pins_down);

Game *Game_Create(void);

/* NULL if count_pins is NULL, or if no game is free. */
Game *Game_CreateWithRule(PinCountRule count_pins);

/* NULL, or a pointer that isn't a game, is ignored. Stops the program (Fault_Stop) if the game
 * is busy or already destroyed. A handle to a slot since reused by another game can't be
 * detected. */
void Game_Destroy(Game *game);

/* Told a frame's number (1 to 10), its score, and whether it is complete: after a roll, about
 * each frame the roll completed, oldest first; after an edit, about every frame again, where a
 * number heard before is an update and complete = false means reopened. A callback may read the
 * game but not change it (GAME_ERR_BUSY). It runs on up to 272 bytes of the library's stack at
 * -O2, 416 at -O0 (64-bit host; measure on the target). */
typedef void (*FrameChangedCallback)(void *context, FrameNumber frame_number, Score frame_score,
                                     bool frame_complete);

/* Adds a listener, and tells it, and only it, about every frame already complete. Room for two.
 * False, adding nothing, if both are taken, if game or callback is NULL, or if the game is
 * busy. */
bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context);

GameStatus Game_Roll(Game *game, Pins pins);

/* Replaces rolls_removed rolls, from roll number first_roll (the first is 1), with the new_count
 * rolls in new_pins: a replacement, an insertion (none removed) or a deletion (none new). */
typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    const Pins *new_pins; /* NULL only when new_count is 0 */
    uint8_t new_count;
} RollEdit;

/* Game_EditRolls with one roll out and one in. */
GameStatus Game_CorrectRoll(Game *game, RollNumber roll_number, Pins pins);

/* Applies the edit, and rescores by replaying every roll; the listeners hear only the final
 * state. Rejected with the status of any roll it makes impossible, or with
 * GAME_ERR_NO_SUCH_ROLL (also for a NULL edit), GAME_ERR_TOO_MANY_ROLLS or GAME_ERR_BUSY. */
GameStatus Game_EditRolls(Game *game, const RollEdit *edit);

/* The total of the complete frames; 0 for a NULL game. */
Score Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
