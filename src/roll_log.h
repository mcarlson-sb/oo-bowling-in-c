#ifndef ROLL_LOG_H
#define ROLL_LOG_H

/* Every accepted roll of a game, as the pins that fell. Plain values only, so a RollLog can be
 * copied safely: saving one before an edit, and putting it back, is how a rejected edit is
 * undone. (A Game or FrameContext can't be copied like that: each context points at its own
 * state.)
 *
 * Private to the library: lives in src/, not include/. */

#include <stdint.h>

#include "bowling_types.h"
#include "game.h"
#include "game_limits.h"

typedef struct {
    Pins pins[GAME_MAX_ROLLS];
    uint8_t count;
} RollLog;

/* An empty log. */
void RollLog_Init(RollLog *self);

/* Adds a roll at the end. The game never accepts more rolls than a log holds. */
void RollLog_Append(RollLog *self, Pins pins);

uint8_t RollLog_Count(const RollLog *self);

/* The roll at `index`, counting from 0. */
Pins RollLog_At(const RollLog *self, uint8_t index);

/* Writes to `edited` this log with `edit` (see RollEdit, in game.h) applied: the rolls before
 * the range, the new rolls, then the rolls after it. Returns GAME_OK, or, writing nothing:
 *   - GAME_ERR_NO_SUCH_ROLL if `edit` is NULL, if the range isn't rolls this log has (so an
 *     edit can't add rolls after the last one), or if new rolls are promised but `new_pins`
 *     is NULL;
 *   - GAME_ERR_TOO_MANY_ROLLS if the edited log would hold more than a game can have. */
GameStatus RollLog_Edit(const RollLog *self, const RollEdit *edit, RollLog *edited);

#endif /* ROLL_LOG_H */
