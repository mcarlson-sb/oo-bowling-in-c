#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

/* What the black-box tests share: a handle that always destroys its game, a way to make the
 * setup rolls, and the client-side rules that more than one test plays with. Everything here
 * uses only the public API, like any caller would. */

#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>

#include "game.h"

/* Games come from a fixed pool that outlives each test. Holding every game in a handle that
 * destroys it means a test can't leak a pool slot into later tests, however it ends. */
using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

inline GameHandle MakeGame()
{
    return GameHandle(Game_Create(), &Game_Destroy);
}

inline GameHandle MakeGameWithRule(PinCountRule count_pins)
{
    return GameHandle(Game_CreateWithRule(count_pins), &Game_Destroy);
}

/* Setup rolls: each must be accepted, or the test is not testing what it says it is. */
inline void RollAll(Game *game, std::initializer_list<Pins> rolls)
{
    for (const Pins pins : rolls) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins << " was rejected";
    }
}

/* ---- Client-side rules ------------------------------------------------------------------ *
 * The library contains none of these. Tests supply them, as a caller would. */

/* Nine-pin no-tap: a first ball, on a full rack, that leaves one pin standing counts as a
 * strike. Every other roll counts as the pins it knocked down. */
inline Pins NinePinNoTap(Pins pins_standing, Pins pins_down)
{
    const bool nine_on_a_full_rack = (pins_standing == 10U) && (pins_down == 9U);
    return nine_on_a_full_rack ? static_cast<Pins>(10U) : pins_down;
}

#endif /* TEST_SUPPORT_H */
