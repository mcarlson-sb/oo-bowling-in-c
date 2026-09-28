#include "roll_edit.h"

#include <stdbool.h>
#include <stddef.h>

#include "game_limits.h"

static inline uint8_t RollNumber_ToIndex(RollNumber roll_number)
{
    return (uint8_t)(roll_number - 1U);
}

static bool RollEdit_StartsAtARoll(const RollEdit *edit, const RollLog *log)
{
    return (edit->first_roll != 0U) && (edit->first_roll <= RollLog_Count(log));
}

static bool RollEdit_RemovesOnlyExistingRolls(const RollEdit *edit, const RollLog *log)
{
    const uint8_t first_index = RollNumber_ToIndex(edit->first_roll);
    return (first_index + edit->rolls_removed) <= RollLog_Count(log);
}

/* Starts at a roll the log has, before asking what it removes: roll 0 has no index. */
static bool RollEdit_IsWithinLog(const RollEdit *edit, const RollLog *log)
{
    return RollEdit_StartsAtARoll(edit, log) && RollEdit_RemovesOnlyExistingRolls(edit, log);
}

static bool RollEdit_PromisesRollsWithoutPins(const RollEdit *edit)
{
    return (edit->new_pins == NULL) && (edit->new_count > 0U);
}

/* Only once RollEdit_IsWithinLog holds: more rolls removed than the log has would wrap. */
static unsigned RollEdit_ResultingRollCount(const RollEdit *edit, const RollLog *log)
{
    return ((unsigned)RollLog_Count(log) - edit->rolls_removed) + edit->new_count;
}

static GameStatus RollEdit_Check(const RollEdit *edit, const RollLog *log)
{
    if (edit == NULL) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (!RollEdit_IsWithinLog(edit, log)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (RollEdit_PromisesRollsWithoutPins(edit)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (RollEdit_ResultingRollCount(edit, log) > GAME_MAX_ROLLS) {
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
