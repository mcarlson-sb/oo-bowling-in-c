#ifndef ROLL_EDIT_H
#define ROLL_EDIT_H

/* The rules of an edit (a RollEdit, from game.h): which edits a roll log can take, and the log
 * each one makes. */

#include "game.h"
#include "roll_log.h"

/* Writes `log` with `edit` applied to `edited`, or, writing nothing, returns
 * GAME_ERR_NO_SUCH_ROLL (a NULL edit, a range outside the log, or new_pins missing) or
 * GAME_ERR_TOO_MANY_ROLLS. */
GameStatus RollEdit_Apply(const RollEdit *edit, const RollLog *log, RollLog *edited);

#endif /* ROLL_EDIT_H */
