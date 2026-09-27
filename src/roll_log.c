#include "roll_log.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

void RollLog_Init(RollLog *self)
{
    self->count = 0U;
}

void RollLog_Append(RollLog *self, Pins pins)
{
    assert(self->count < GAME_MAX_ROLLS);
    if (self->count >= GAME_MAX_ROLLS) {
        return;
    }
    self->pins[self->count] = pins;
    self->count++;
}

uint8_t RollLog_Count(const RollLog *self)
{
    return self->count;
}

Pins RollLog_At(const RollLog *self, uint8_t index)
{
    assert(index < self->count);
    if (index >= self->count) {
        return 0U;
    }
    return self->pins[index];
}

static inline uint8_t RollNumber_ToIndex(RollNumber roll_number)
{
    return (uint8_t)(roll_number - 1U);
}

static bool RollLog_HasRange(const RollLog *self, const RollEdit *edit)
{
    const uint8_t first_index = RollNumber_ToIndex(edit->first_roll);
    return (edit->first_roll != 0U) && (edit->first_roll <= self->count) &&
           ((first_index + edit->rolls_removed) <= self->count);
}

GameStatus RollLog_Edit(const RollLog *self, const RollEdit *edit, RollLog *edited)
{
    if (edit == NULL) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if (!RollLog_HasRange(self, edit)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    if ((edit->new_pins == NULL) && (edit->new_count > 0U)) {
        return GAME_ERR_NO_SUCH_ROLL;
    }
    const unsigned new_length = ((unsigned)self->count - edit->rolls_removed) + edit->new_count;
    if (new_length > GAME_MAX_ROLLS) {
        return GAME_ERR_TOO_MANY_ROLLS;
    }

    const uint8_t first_index = RollNumber_ToIndex(edit->first_roll);
    edited->count = 0U;
    for (uint8_t i = 0U; i < first_index; i++) {
        edited->pins[edited->count++] = self->pins[i];
    }
    for (uint8_t i = 0U; i < edit->new_count; i++) {
        edited->pins[edited->count++] = edit->new_pins[i];
    }
    for (uint8_t i = (uint8_t)(first_index + edit->rolls_removed); i < self->count; i++) {
        edited->pins[edited->count++] = self->pins[i];
    }
    return GAME_OK;
}
