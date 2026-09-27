#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

/* Shared by the black-box tests; public API only. */

#include <gtest/gtest.h>

#include <initializer_list>
#include <map>
#include <memory>

#include "game.h"

/* So no test, however it ends, leaks a pool slot into the next. */
using GameHandle = std::unique_ptr<Game, decltype(&Game_Destroy)>;

inline GameHandle MakeGame()
{
    return GameHandle(Game_Create(), &Game_Destroy);
}

inline GameHandle MakeGameWithRule(PinCountRule count_pins)
{
    return GameHandle(Game_CreateWithRule(count_pins), &Game_Destroy);
}

/* Setup rolls: each must be accepted, or the test isn't testing what it says. */
inline void RollAll(Game *game, std::initializer_list<Pins> rolls)
{
    for (const Pins pins : rolls) {
        EXPECT_EQ(GAME_OK, Game_Roll(game, pins)) << "setup roll of " << +pins << " was rejected";
    }
}

/* C++17 has no designated initializers. */
inline RollEdit MakeEdit(RollNumber first_roll, uint8_t rolls_removed, const Pins *new_pins,
                         uint8_t new_count)
{
    return RollEdit{first_roll, rolls_removed, new_pins, new_count};
}

/* ---- Client-side subscribers ------------------------------------------------------------ */

/* Keyed by frame, so a frame told again after a correction is an update. */
struct RunningStats {
    std::map<int, int> frame_scores; /* frame number -> latest score */

    int Frames() const { return static_cast<int>(frame_scores.size()); }
    double Average() const
    {
        int total = 0;
        for (const auto &frame : frame_scores) {
            total += frame.second;
        }
        return frame_scores.empty() ? 0.0 : static_cast<double>(total) / Frames();
    }
};

inline void RunningStats_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                      bool frame_complete)
{
    auto *stats = static_cast<RunningStats *>(context);
    if (frame_complete) {
        stats->frame_scores[frame_number] = frame_score;
    } else {
        stats->frame_scores.erase(frame_number); /* reopened by a correction */
    }
}

/* ---- Client-side rules ------------------------------------------------------------------ */

/* A first ball that leaves one pin standing counts as a strike. */
inline Pins NinePinNoTap(Pins pins_standing, Pins pins_down)
{
    const bool nine_on_a_full_rack = (pins_standing == 10U) && (pins_down == 9U);
    return nine_on_a_full_rack ? static_cast<Pins>(10U) : pins_down;
}

#endif /* TEST_SUPPORT_H */
