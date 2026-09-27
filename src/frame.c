#include "frame.h"

#include <assert.h>
#include <stddef.h>

void Frame_Init(Frame *self, const FrameVtable *vtable)
{
    self->vtable = vtable;
    RollList_Init(&self->rolls);
    RollList_Init(&self->bonus_rolls);
    self->complete = false;
}

RollResult Frame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    assert(context != NULL);
    /* Once here, so no state's roll() ever sees a complete frame. */
    if (self->complete) {
        return RollResult_Passed(pins);
    }
    return self->vtable->roll(self, context, pins);
}

Pins Frame_PinsStanding(const Frame *self)
{
    return self->vtable->pins_standing(self);
}

Pins Frame_AllPinsStanding(const Frame *self)
{
    (void)self;
    return FRAME_ALL_PINS;
}

void Frame_AddRoll(Frame *self, Pins pins)
{
    RollList_Add(&self->rolls, pins);
}

void Frame_AddBonusRoll(Frame *self, Pins pins)
{
    RollList_Add(&self->bonus_rolls, pins);
}

void Frame_Complete(Frame *self)
{
    self->complete = true;
}

static void Frame_CopyRolls(Frame *self, const Frame *from)
{
    for (uint8_t i = 0U; i < RollList_Count(&from->rolls); i++) {
        Frame_AddRoll(self, RollList_At(&from->rolls, i));
    }
}

Frame *Frame_InitStrike(Frame *self, const FrameVtable *vtable)
{
    Frame_Init(self, vtable);
    Frame_AddRoll(self, FRAME_ALL_PINS);
    return self;
}

Frame *Frame_InitSpare(Frame *self, const FrameVtable *vtable, const Frame *replaced,
                       Pins completing_pins)
{
    Frame_Init(self, vtable);
    Frame_CopyRolls(self, replaced);
    Frame_AddRoll(self, completing_pins);
    return self;
}

Pins Frame_PinsKnockedDown(const Frame *self)
{
    return RollList_Sum(&self->rolls);
}

bool Frame_NextIsFirstRoll(const Frame *self)
{
    return RollList_Count(&self->rolls) == 0U;
}

bool Frame_NextIsSecondBonusRoll(const Frame *self)
{
    return RollList_Count(&self->bonus_rolls) == 1U;
}

Pins Frame_FirstBonusRoll(const Frame *self)
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

bool Frame_IsComplete(const Frame *self)
{
    return self->complete;
}

Score Frame_Score(const Frame *self)
{
    if (!self->complete) {
        return 0U;
    }
    return (Score)(RollList_Sum(&self->rolls) + RollList_Sum(&self->bonus_rolls));
}
