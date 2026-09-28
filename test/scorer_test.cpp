/* The scorer core: a pure, data-driven scorer with no callbacks. Each operation returns a
 * status, and writes the frame changes it caused to a buffer the caller supplies. */

#include <gtest/gtest.h>

#include <initializer_list>

#include "scorer.h"

namespace {

Scorer MakeScorer(ScorerVariant variant)
{
    Scorer scorer;
    Scorer_Init(&scorer, variant);
    return scorer;
}

/* Setup balls: each must be accepted, or the test isn't testing what it says. */
void RollAll(Scorer *scorer, std::initializer_list<Pins> balls)
{
    for (const Pins pins : balls) {
        FrameEvents events;
        ASSERT_EQ(GAME_OK, Scorer_Roll(scorer, pins, &events)) << "setup ball of " << +pins;
    }
}

void RollMany(Scorer *scorer, int count, Pins pins)
{
    for (int i = 0; i < count; i++) {
        RollAll(scorer, {pins});
    }
}

} // namespace

TEST(TenPinScorerTest, should_add_up_open_frames)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 20, 1U);
    EXPECT_EQ(20U, Scorer_Score(&scorer));
}

TEST(TenPinScorerTest, should_add_a_spares_next_ball_as_its_bonus)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {5U, 5U, 3U});
    RollMany(&scorer, 17, 0U);
    EXPECT_EQ(16U, Scorer_Score(&scorer)); /* 5 + 5 + 3, then 3 */
}

TEST(TenPinScorerTest, should_add_a_strikes_next_two_balls_as_its_bonus)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {10U, 3U, 4U});
    RollMany(&scorer, 16, 0U);
    EXPECT_EQ(24U, Scorer_Score(&scorer)); /* 10 + 3 + 4, then 3 + 4 */
}
