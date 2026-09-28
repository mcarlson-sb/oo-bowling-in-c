/* The scorer core: a pure, data-driven scorer with no callbacks. Each operation returns a
 * status, and writes the frame changes it caused to a buffer the caller supplies. */

#include <gtest/gtest.h>

#include <initializer_list>
#include <random>
#include <vector>

#include "scorer.h"
#include "ten_pin_reference.h"

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

TEST(TenPinScorerTest, should_leave_a_strike_out_of_the_total_until_its_bonus_is_known)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {10U, 3U});
    EXPECT_EQ(0U, Scorer_Score(&scorer)); /* the strike still waits for one ball */
    RollAll(&scorer, {4U});
    EXPECT_EQ(24U, Scorer_Score(&scorer)); /* 17, and frame 2 is complete after two balls */
}

TEST(TenPinScorerTest, should_score_a_perfect_game_as_300)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 12, 10U); /* the last two are the tenth frame's fill balls */
    EXPECT_EQ(300U, Scorer_Score(&scorer));
}

TEST(TenPinScorerTest, should_reject_a_ball_after_the_game_is_over)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 19, 0U);
    RollAll(&scorer, {3U}); /* an open tenth frame ends the game */
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_GAME_OVER, Scorer_Roll(&scorer, 4U, &events));
    EXPECT_EQ(3U, Scorer_Score(&scorer));
    EXPECT_EQ(0U, events.count);
}

TEST(TenPinScorerTest, should_reject_more_pins_than_are_standing)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {6U});
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Roll(&scorer, 5U, &events));
    RollAll(&scorer, {4U, 3U}); /* unchanged: the 4 is still the spare's second ball */
    EXPECT_EQ(13U, Scorer_Score(&scorer));
}

TEST(TenPinScorerTest, should_reject_a_fill_ball_with_more_pins_than_are_standing)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 18, 0U);
    RollAll(&scorer, {10U, 3U});
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Roll(&scorer, 8U, &events)); /* 7 standing */
    RollAll(&scorer, {7U});
    EXPECT_EQ(20U, Scorer_Score(&scorer));
}

/* ---- The frames a ball changed, written to the caller's buffer ------------------------- */

namespace {

struct Event {
    int frame;
    int score;
    bool complete;
    bool operator==(const Event &other) const
    {
        return (frame == other.frame) && (score == other.score) && (complete == other.complete);
    }
};

std::vector<Event> EventsOf(const FrameEvents &events)
{
    std::vector<Event> list;
    for (uint8_t i = 0U; i < events.count; i++) {
        const FrameEvent &event = events.events[i];
        list.push_back({event.frame_number, event.frame_score, event.frame_complete});
    }
    return list;
}

std::vector<Event> EventsOfRoll(Scorer *scorer, Pins pins)
{
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Roll(scorer, pins, &events));
    return EventsOf(events);
}

} // namespace

TEST(TenPinScorerEventsTest, should_report_each_frame_a_ball_completes_oldest_first)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    EXPECT_EQ((std::vector<Event>{}), EventsOfRoll(&scorer, 10U));
    EXPECT_EQ((std::vector<Event>{}), EventsOfRoll(&scorer, 3U));
    /* The 4 completes the strike (17) and its own frame (7). */
    EXPECT_EQ((std::vector<Event>{{1, 17, true}, {2, 7, true}}), EventsOfRoll(&scorer, 4U));
}

/* ---- Against the independent reference ------------------------------------------------- */

TEST(TenPinScorerTest, should_score_random_games_as_the_reference_does)
{
    /* Random legal games of random length, with extra strikes and spares, checked after every
     * ball. The reference plays its own lane to know how many pins stand. */
    std::mt19937 random(20260928U);
    for (int game_number = 0; game_number < 2000; ++game_number) {
        Scorer scorer = MakeScorer(SCORER_TEN_PIN);
        ten_pin_reference::Lane lane;
        std::vector<int> balls;
        const int stop_after = std::uniform_int_distribution<int>(1, 21)(random);
        for (int ball = 0; (ball < stop_after) && !lane.over; ++ball) {
            const bool clear_the_rack = std::uniform_int_distribution<int>(0, 3)(random) == 0;
            const int down =
                clear_the_rack ? lane.standing
                               : std::uniform_int_distribution<int>(0, lane.standing)(random);
            RollAll(&scorer, {static_cast<Pins>(down)});
            balls.push_back(down);
            lane.Roll(down);
            ASSERT_EQ(ten_pin_reference::Score(balls), static_cast<int>(Scorer_Score(&scorer)))
                << "game " << game_number << ", after ball " << (ball + 1);
        }
        FrameEvents events;
        if (lane.over) {
            ASSERT_EQ(GAME_ERR_GAME_OVER, Scorer_Roll(&scorer, 0U, &events))
                << "game " << game_number;
        }
    }
}
