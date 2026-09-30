#include "held_rolls.h"

void HeldRolls_Init(HeldRolls *self)
{
    self->count = 0U;
    self->first_refused_for = GAME_OK;
    self->lost = 0U;
}

bool HeldRolls_IsEmpty(const HeldRolls *self)
{
    return self->count == 0U;
}

uint8_t HeldRolls_Count(const HeldRolls *self)
{
    return self->count;
}

Pins HeldRolls_PinsAt(const HeldRolls *self, uint8_t index)
{
    return self->pins[index];
}

uint8_t HeldRolls_Newest(const HeldRolls *self)
{
    return (uint8_t)(self->count - 1U);
}

bool HeldRolls_Hold(HeldRolls *self, Pins pins)
{
    if (self->count == SCORER_MAX_BALLS) {
        self->lost++;
        return false;
    }
    self->pins[self->count] = pins;
    self->count++;
    return true;
}

uint16_t HeldRolls_Lost(const HeldRolls *self)
{
    return self->lost;
}

GameStatus HeldRolls_WhyFirstRefused(const HeldRolls *self)
{
    return self->first_refused_for;
}

void HeldRolls_RefuseFirst(HeldRolls *self, GameStatus why)
{
    self->first_refused_for = why;
}

void HeldRolls_DropFirst(HeldRolls *self)
{
    self->count--;
    for (uint8_t i = 0U; i < self->count; i++) {
        self->pins[i] = self->pins[i + 1U];
    }
}
