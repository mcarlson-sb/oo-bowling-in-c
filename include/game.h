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
    /* Game_Roll, Game_CorrectRoll, Game_EditRolls or Pinsetter_Drain was called while the
     * game was busy with another roll or edit: from inside a frame-changed callback, or from
     * inside the game's PinCountRule. A change then would happen under the one in progress,
     * and tell the listeners about frames out of order or twice, so it is rejected and the
     * game is left unchanged. Call again once the callback or the rule has returned. */
    GAME_ERR_DURING_NOTIFICATION,
    /* Game_CorrectRoll or Game_EditRolls was given a roll number the game hasn't had (rolls
     * start at 1), or a range that runs past the last roll. Nothing is changed. */
    GAME_ERR_NO_SUCH_ROLL,
    /* Game_EditRolls would leave more rolls than any game can have (21). Nothing is
     * changed. */
    GAME_ERR_TOO_MANY_ROLLS
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
 *     it is the caller's to keep. One consequence is caught: when an edit is rejected, the
 *     game undoes it by replaying the rolls it had, and if an impure rule makes that replay
 *     fail too, there is no game left that is known to be right, so the program stops
 *     (Fault_Stop, in every build).
 *
 * Stack: a rule runs on top of the library's own frames. The most under it is during a
 * correction's replay: 432 bytes at -O2, 368 at -O0 (208 and 128 during Game_Roll). More at -O2
 * than at -O0 is not a typo: optimizing merges the edit into one large frame, while the replay
 * that calls the rule stays two calls further down. Those are 64-bit host numbers from GCC 16,
 * to show the scale; a target has to measure its own (KAY.md, "Worst-case stack depth", shows
 * how). */
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
 * A callback may read the game (Game_Score sees the whole roll), but not change it: a roll,
 * an edit or a drain from inside a callback returns GAME_ERR_DURING_NOTIFICATION.
 *
 * Stack: a callback runs on top of the library's own frames. The most under it is after a
 * correction: 272 bytes at -O2, 416 at -O0 (128 and 240 after Game_Roll). Those are 64-bit host
 * numbers from GCC 16, to show the scale; a target has to measure its own (KAY.md, "Worst-case
 * stack depth", shows how). */
typedef void (*FrameChangedCallback)(void *context, FrameNumber frame_number, Score frame_score,
                                     bool frame_complete);

/* Adds a callback this game tells about changed frames; `context` is passed back to it
 * unchanged. A game has room for two. Returns false, adding nothing, if both are taken, or
 * if `game` or `callback` is NULL. */
bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context);

GameStatus Game_Roll(Game *game, Pins pins);

/* One edit of a game's rolls: replace `rolls_removed` rolls, starting at roll number
 * `first_roll` (the first roll is 1), with the `new_count` rolls in `new_pins`. One edit covers
 * every fix: replacing a roll (one out, one in), inserting (none out), and deleting (none in).
 * Four values that always travel together, so they travel as one, by pointer. */
typedef struct {
    RollNumber first_roll;
    uint8_t rolls_removed;
    const Pins *new_pins; /* may be NULL only when new_count is 0 */
    uint8_t new_count;
} RollEdit;

/* Corrects roll number `roll_number` (the first roll is 1) to `pins`, the pins that really
 * fell, and rescores the game by replaying every roll, each counted again by the game's
 * rule. The listeners are told the result (see FrameChangedCallback). Rejected, changing
 * nothing, if it would make any roll impossible (the status of that roll, such as
 * GAME_ERR_INVALID_PINS), if the roll hasn't been made (GAME_ERR_NO_SUCH_ROLL), or if called
 * from inside a callback (GAME_ERR_DURING_NOTIFICATION). */
GameStatus Game_CorrectRoll(Game *game, RollNumber roll_number, Pins pins);

/* Applies `edit` (see RollEdit) to the game's rolls, and rescores the game.
 *
 * The edit is checked, and told to the listeners, only in its final state: they never hear
 * about a game in between. Rejected, changing nothing, if the edited game has an impossible
 * roll (that roll's status), if the range isn't rolls the game has had (so an edit can't add
 * rolls after the last one: that is Game_Roll's job), `new_pins` is NULL with rolls promised,
 * or `edit` itself is NULL (GAME_ERR_NO_SUCH_ROLL), if it would make more than 21 rolls
 * (GAME_ERR_TOO_MANY_ROLLS), or if called from inside a callback
 * (GAME_ERR_DURING_NOTIFICATION). */
GameStatus Game_EditRolls(Game *game, const RollEdit *edit);
Score Game_Score(const Game *game);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */
