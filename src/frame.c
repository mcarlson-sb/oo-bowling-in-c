#include "frame.h"

void Frame_Init(Frame *self, const FrameVtable *vtable)
{
    self->vtable = vtable;
    RollList_Init(&self->rolls);
    RollList_Init(&self->bonus_rolls);
    self->complete = false;
}

RollResult Frame_Roll(Frame *self, struct FrameContext *context, uint8_t pins)
{
    /* A complete frame passes every roll on. Handled once here, so each state's roll() only
     * ever sees rolls while its frame is still incomplete. */
    if (self->complete) {
        return RollResult_Passed(pins);
    }
    return self->vtable->roll(self, context, pins);
}

uint8_t Frame_PinsStanding(const Frame *self)
{
    return self->vtable->pins_standing(self);
}

uint8_t Frame_AllPinsStanding(const Frame *self)
{
    (void)self;
    return FRAME_ALL_PINS;
}

void Frame_AddRoll(Frame *self, uint8_t pins)
{
    RollList_Add(&self->rolls, pins);
}

void Frame_AddBonusRoll(Frame *self, uint8_t pins)
{
    RollList_Add(&self->bonus_rolls, pins);
}

void Frame_Complete(Frame *self)
{
    self->complete = true;
}

void Frame_CopyRolls(Frame *self, const Frame *from)
{
    for (uint8_t i = 0U; i < RollList_Count(&from->rolls); i++) {
        Frame_AddRoll(self, RollList_At(&from->rolls, i));
    }
}

uint8_t Frame_PinsKnockedDown(const Frame *self)
{
    return RollList_Sum(&self->rolls);
}

bool Frame_IsFirstRoll(const Frame *self)
{
    return RollList_Count(&self->rolls) == 0U;
}

bool Frame_IsSecondRoll(const Frame *self)
{
    return RollList_Count(&self->rolls) == 1U;
}

bool Frame_IsSecondBonusRoll(const Frame *self)
{
    return RollList_Count(&self->bonus_rolls) == 1U;
}

uint8_t Frame_FirstBonusRoll(const Frame *self)
{
    return RollList_At(&self->bonus_rolls, 0U);
}

bool Frame_HasAllRolls(const Frame *self)
{
    return RollList_IsFull(&self->rolls);
}

bool Frame_HasAllBonusRolls(const Frame *self)
{
    return RollList_IsFull(&self->bonus_rolls);
}

uint16_t Frame_Score(const Frame *self)
{
    if (!self->complete) {
        return 0U;
    }
    return (uint16_t)(RollList_Sum(&self->rolls) + RollList_Sum(&self->bonus_rolls));
}
