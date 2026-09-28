#include "roll_edit.h"

#include <stdbool.h>
#include <stddef.h>

#include "game_limits.h"

static inline uint8_t RollNumber_ToIndex(RollNumber roll_number)
{
    return (uint8_t)(roll_number - 1U);
}

static bool RollEdit_HasRange(const RollEdit *edit, const RollLog *log)
{
    const uint8_t count = RollLog_Count(log);
    const uint8_t first_index = RollNumber_ToIndex(edit->first_roll);
    return (edit->first_roll != 0U) && (edit->first_roll <= count) &&
           ((first_index + edit->rolls_removed) <= count);
}

static GameStatus RollEdit_Check(const RollEdit *edit, const RollLog *log)
{
    if (edit == NULL) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (!RollEdit_HasRange(edit, log)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if ((edit->new_pins == NULL) && (edit->new_count > 0U)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    const unsigned new_length =
        ((unsigned)RollLog_Count(log) - edit->rolls_removed) + edit->new_count;
    if (new_length > GAME_MAX_ROLLS) {
        return GAME_ERR_TOO_MANY_ROLLS;
    }
    return GAME_OK;
}

static void RollEdit_Splice(const RollEdit *edit, const RollLog *log, RollLog *edited)
{
    const uint8_t first_index = RollNumber_ToIndex(edit->first_roll);
    const uint8_t after_removed = (uint8_t)(first_index + edit->rolls_removed);
    RollLog_Init(edited);
    RollLog_AppendRange(edited, log, 0U, first_index);
    RollLog_AppendPins(edited, edit->new_pins, edit->new_count);
    RollLog_AppendRange(edited, log, after_removed, RollLog_Count(log));
}

GameStatus RollEdit_Apply(const RollEdit *edit, const RollLog *log, RollLog *edited)
{
    const GameStatus status = RollEdit_Check(edit, log);
    if (status == GAME_OK) {
        RollEdit_Splice(edit, log, edited);
    }
    return status;
}
