#include "roll_log.h"

#include <stddef.h>

void RollLog_Init(RollLog *self)
{
    self->count = 0U;
}

void RollLog_Append(RollLog *self, Pins pins)
{
    self->pins[self->count] = pins;
    self->count++;
}

uint8_t RollLog_Count(const RollLog *self)
{
    return self->count;
}

Pins RollLog_At(const RollLog *self, uint8_t index)
{
    return self->pins[index];
}

GameStatus RollLog_Edit(const RollLog *self, const RollEdit *edit, RollLog *edited)
{
    const uint8_t first = (uint8_t)(edit->first_roll - 1U); /* index of the first roll replaced */
    if ((edit->first_roll == 0U) || (edit->first_roll > self->count) ||
        ((first + edit->rolls_removed) > self->count)) {
        return GAME_ERR_NO_SUCH_ROLL; /* an edit starts at a roll the game has had */
    }
    if ((edit->new_pins == NULL) && (edit->new_count > 0U)) {
        return GAME_ERR_NO_SUCH_ROLL; /* new rolls promised, but none given */
    }
    const unsigned new_length = ((unsigned)self->count - edit->rolls_removed) + edit->new_count;
    if (new_length > GAME_MAX_ROLLS) {
        return GAME_ERR_TOO_MANY_ROLLS; /* no game has that many, and the log has no room */
    }

    edited->count = 0U;
    for (uint8_t i = 0U; i < first; i++) {
        edited->pins[edited->count++] = self->pins[i];
    }
    for (uint8_t i = 0U; i < edit->new_count; i++) {
        edited->pins[edited->count++] = edit->new_pins[i];
    }
    for (uint8_t i = (uint8_t)(first + edit->rolls_removed); i < self->count; i++) {
        edited->pins[edited->count++] = self->pins[i];
    }
    return GAME_OK;
}
