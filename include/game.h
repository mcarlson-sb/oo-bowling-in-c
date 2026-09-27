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
    GAME_ERR_RULE_OUT_OF_RANGE,
    /* Game_CorrectRoll or Game_EditRolls was called from inside a frame-changed callback.
     * Replaying the game then would tell listeners about frames out of order, or twice, so
     * it is rejected and the game is left unchanged. Call again once the callback has
     * returned. (A roll from inside a callback is queued instead: see GAME_QUEUED.)
     * Pinsetter_Drain returns it too, from inside a callback, and drains nothing. */
    GAME_ERR_EDIT_DURING_NOTIFICATION,
    /* Game_CorrectRoll or Game_EditRolls was given a roll number the game hasn't had (rolls
     * start at 1), or a range that runs past the last roll. Nothing is changed. */
    GAME_ERR_NO_SUCH_ROLL,
    /* Game_EditRolls would leave more rolls than any game can have (21), or a callback
     * queued more rolls than the game has left. Nothing is changed. */
    GAME_ERR_TOO_MANY_ROLLS,
    /* Not an error. Game_Roll was called from inside a frame-changed callback, so the roll
     * went into the game's mailbox. It is checked and applied, like any roll, as soon as
     * the listeners have heard about the roll before it. */
    GAME_QUEUED
} GameStatus;

/* How many pins a roll counts as, given how many were standing before it and how many it
 * knocked down. A caller supplies one to play a variant of the game; standard bowling
 * counts exactly the pins that fell.
 *
 * The contract a rule must keep:
 *   - It returns no more than pins_standing. Game_Roll rejects a roll whose count is out of
 *     range (GAME_ERR_RULE_OUT_OF_RANGE).
 *   - It is a pure function of its two arguments: the same pins standing and pins down
 *     always give the same count, with nothing read from anywhere else. Every correction
 *     (Game_CorrectRoll) replays every roll of the game through the rule, so a rule that
 *     depends on anything else can silently rewrite the game's history. Nothing checks this;
 *     it is the caller's to keep. */
typedef Pins (*PinCountRule)(Pins pins_standing, Pins pins_down);

/* A standard game. */
Game *Game_Create(void);

/* A game whose rolls are counted by `count_pins`. NULL if `count_pins` is NULL, or if no
 * game is free. */
Game *Game_CreateWithRule(PinCountRule count_pins);

void Game_Destroy(Game *game);

/* Told when a frame changes: the frame's number (1 to 10), its score, and whether it is
 * complete.
 *   - After a roll, it is told about each frame the roll completed, oldest first.
 *   - After a correction (Game_CorrectRoll), it is told again about every complete frame,
 *     with its new score: a frame number it has heard before is an update.
 * A callback may read the game (Game_Score sees the whole roll). It may also roll: Game_Roll
 * from inside a callback returns GAME_QUEUED, and the roll is applied once the listeners
 * have heard about the roll before it. */
typedef void (*FrameChangedCallback)(void *context, uint8_t frame_number, Score frame_score,
                                     bool frame_complete);

/* Adds a callback this game tells about changed frames; `context` is passed back to it
 * unchanged. A game has room for two. Returns false, adding nothing, if both are taken, or
 * if `game` or `callback` is NULL. */
bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context);

GameStatus Game_Roll(Game *game, Pins pins);

/* Corrects roll number `roll_number` (the first roll is 1) to `pins`, the pins that really
 * fell, and rescores the game by replaying every roll, each counted again by the game's
 * rule. The listeners are told the result (see FrameChangedCallback). Rejected, changing
 * nothing, if it would make any roll impossible (the status of that roll, such as
 * GAME_ERR_INVALID_PINS), if the roll hasn't been made (GAME_ERR_NO_SUCH_ROLL), or if called
 * from inside a callback (GAME_ERR_EDIT_DURING_NOTIFICATION). */
GameStatus Game_CorrectRoll(Game *game, uint8_t roll_number, Pins pins);

/* Replaces `rolls_removed` rolls, starting at roll number `first_roll` (the first roll is 1),
 * with the `new_count` rolls in `new_pins`, and rescores the game. One edit covers every fix:
 * replacing a roll (one out, one in), inserting (none out), and deleting (none in).
 *
 * The edit is checked, and told to the listeners, only in its final state: they never hear
 * about a game in between. Rejected, changing nothing, if the edited game has an impossible
 * roll (that roll's status), if the range isn't rolls the game has had (so an edit can't add
 * rolls after the last one: that is Game_Roll's job) or `new_pins` is NULL
 * with rolls promised (GAME_ERR_NO_SUCH_ROLL), if it would make more than 21 rolls
 * (GAME_ERR_TOO_MANY_ROLLS), or if called from inside a callback
 * (GAME_ERR_EDIT_DURING_NOTIFICATION). */
GameStatus Game_EditRolls(Game *game, uint8_t first_roll, uint8_t rolls_removed,
                          const Pins *new_pins, uint8_t new_count);
Score Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
