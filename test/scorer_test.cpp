/* The scorer core: a pure, data-driven scorer with no callbacks. Each operation returns a
 * status, and writes the frame changes it caused to a buffer the caller supplies. */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <random>
#include <vector>

#include "scorer.h"
#include "candlepin_reference.h"
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

/* ---- Edits: check, replay, and report only the final state ----------------------------- */

namespace {

RollEdit Replace(RollNumber first_ball, uint8_t removed, const std::vector<Pins> &new_pins)
{
    return RollEdit{first_ball, removed, new_pins.empty() ? nullptr : new_pins.data(),
                    static_cast<uint8_t>(new_pins.size())};
}

} // namespace

TEST(TenPinScorerEditTest, should_rescore_and_report_every_complete_frame_after_a_correction)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U, 5U, 5U, 2U}); /* 7, then a spare waiting... 12 */
    const std::vector<Pins> five = {5U};
    const RollEdit edit = Replace(1U, 1U, five);
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ((std::vector<Event>{{1, 9, true}, {2, 12, true}}), EventsOf(events));
    EXPECT_EQ(21U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_reject_an_edit_that_makes_a_ball_impossible_and_change_nothing)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U, 5U, 5U, 2U});
    const std::vector<Pins> eight = {8U};
    const RollEdit edit = Replace(2U, 1U, eight); /* 3 then 8: 11 pins */
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(0U, events.count);
    EXPECT_EQ(19U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_report_a_frame_the_edit_reopened_as_not_complete)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {5U, 5U, 2U}); /* frame 1, a spare, is complete at 12 */
    const RollEdit edit = Replace(3U, 1U, {}); /* delete the 2: the spare waits again */
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ((std::vector<Event>{{1, 0, false}}), EventsOf(events));
    EXPECT_EQ(0U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_reject_an_edit_outside_the_balls_the_game_has_had)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U});
    const std::vector<Pins> one = {1U};
    FrameEvents events;
    const RollEdit at_zero = Replace(0U, 1U, one);         /* balls count from 1 */
    const RollEdit past_the_end = Replace(3U, 0U, one);    /* adding a ball is a roll */
    const RollEdit removes_too_many = Replace(2U, 2U, one); /* only one ball from 2 on */
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Scorer_Edit(&scorer, &at_zero, &events));
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Scorer_Edit(&scorer, &past_the_end, &events));
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Scorer_Edit(&scorer, &removes_too_many, &events));
    EXPECT_EQ(7U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_reject_a_null_edit_or_one_that_promises_balls_it_does_not_give)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U});
    FrameEvents events;
    const RollEdit no_pins = {1U, 1U, nullptr, 1U};
    EXPECT_EQ(GAME_ERR_INVALID_EDIT, Scorer_Edit(&scorer, nullptr, &events));
    EXPECT_EQ(GAME_ERR_INVALID_EDIT, Scorer_Edit(&scorer, &no_pins, &events));
    EXPECT_EQ(7U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_reject_an_edit_that_leaves_more_balls_than_a_game_can_have)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 12, 10U); /* a perfect game, 12 balls */
    RollAll(&scorer, {});
    const std::vector<Pins> ten_ones(10U, 1U);
    const RollEdit edit = Replace(1U, 0U, ten_ones); /* 22 balls */
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_TOO_MANY_ROLLS, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(300U, Scorer_Score(&scorer));
}

/* ---- A counting rule as data, not a callback ------------------------------------------- */

TEST(NoTapScorerTest, should_score_a_first_ball_nine_as_a_strike)
{
    Scorer scorer;
    Scorer_InitWithRule(&scorer, SCORER_TEN_PIN, SCORER_COUNT_NO_TAP);
    RollAll(&scorer, {9U, 3U, 4U});
    EXPECT_EQ(24U, Scorer_Score(&scorer)); /* (10 + 3 + 4) + (3 + 4) */
}

namespace {

Scorer MakeNoTapScorer()
{
    Scorer scorer;
    Scorer_InitWithRule(&scorer, SCORER_TEN_PIN, SCORER_COUNT_NO_TAP);
    return scorer;
}

} // namespace

TEST(NoTapScorerTest, should_count_a_no_tap_strike_as_ten_in_an_earlier_strikes_bonus)
{
    Scorer scorer = MakeNoTapScorer();
    RollAll(&scorer, {10U, 9U, 3U, 4U});
    EXPECT_EQ(47U, Scorer_Score(&scorer)); /* (10 + 10 + 3) + (10 + 3 + 4) + (3 + 4) */
}

TEST(NoTapScorerTest, should_count_no_tap_strikes_in_the_tenth_frames_fill_balls)
{
    Scorer scorer = MakeNoTapScorer();
    RollMany(&scorer, 18, 0U);
    RollAll(&scorer, {10U, 9U, 9U}); /* in standard bowling the second 9 would be too many pins */
    EXPECT_EQ(30U, Scorer_Score(&scorer));
}

TEST(NoTapScorerTest, should_not_count_5_then_4_as_a_spare_under_a_first_ball_rule)
{
    Scorer scorer = MakeNoTapScorer();
    RollAll(&scorer, {5U, 4U, 3U});
    EXPECT_EQ(9U, Scorer_Score(&scorer)); /* an open 5 + 4; the 3 starts frame 2 */
}

/* ---- Pinned by mutation testing: each fails against a mutant that survived -------------- */

TEST(TenPinScorerTest, should_give_a_fill_ball_a_fresh_rack_of_ten_after_one_clears_it)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 18, 0U);
    RollAll(&scorer, {10U, 10U}); /* the first fill ball clears its rack: ten stand again */
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Roll(&scorer, 11U, &events));
    RollAll(&scorer, {10U});
    EXPECT_EQ(30U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_accept_an_edit_that_leaves_exactly_the_most_balls_a_game_can_have)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 18, 0U);
    RollAll(&scorer, {10U, 10U, 10U}); /* 21 balls, the most a game can have */
    const std::vector<Pins> one = {1U};
    const RollEdit edit = Replace(1U, 1U, one); /* still 21 */
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(31U, Scorer_Score(&scorer));
}

TEST(TenPinScorerEditTest, should_put_new_balls_where_the_edit_starts_when_it_starts_after_ball_one)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U, 2U, 1U});
    const std::vector<Pins> spare = {5U, 5U};
    const RollEdit edit = Replace(3U, 1U, spare); /* 3, 4 | 5, 5 | 1 */
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ((std::vector<Event>{{1, 7, true}, {2, 11, true}}), EventsOf(events));
    EXPECT_EQ(18U, Scorer_Score(&scorer));
}

/* ---- Questions a caller can ask ------------------------------------------------------- */

TEST(TenPinScorerQueryTest, should_report_each_frame_as_unknown_until_it_is_complete)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {10U, 3U});
    EXPECT_EQ(2U, Scorer_FramesStarted(&scorer));
    EXPECT_FALSE(Scorer_Frame(&scorer, 0U).complete); /* the strike waits for one more ball */
    RollAll(&scorer, {4U});
    const ScorerFrame first = Scorer_Frame(&scorer, 0U);
    EXPECT_TRUE(first.complete);
    EXPECT_EQ(17U, first.score);
}

TEST(TenPinScorerQueryTest, should_report_the_pins_standing_and_when_the_game_is_over)
{
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    EXPECT_EQ(10U, Scorer_PinsStanding(&scorer));
    RollAll(&scorer, {6U});
    EXPECT_EQ(4U, Scorer_PinsStanding(&scorer));
    RollAll(&scorer, {4U});
    EXPECT_EQ(10U, Scorer_PinsStanding(&scorer)); /* a new frame, a fresh rack */
    RollMany(&scorer, 17, 0U);
    EXPECT_FALSE(Scorer_IsOver(&scorer));
    RollAll(&scorer, {0U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
}

/* ---- Candlepin: the brief's acceptance examples, A1 to A22 --------------------------- */

namespace {

Scorer MakeCandlepin()
{
    return MakeScorer(SCORER_CANDLEPIN);
}

} // namespace

TEST(CandlepinTest, A1_should_score_thirty_zeros_as_0_with_the_game_over_after_ball_30)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 29, 0U);
    EXPECT_FALSE(Scorer_IsOver(&scorer));
    RollAll(&scorer, {0U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(0U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A02_should_twelve_strikes_score_300_with_the_game_over_after_ball_12)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 11, 10U);
    EXPECT_FALSE(Scorer_IsOver(&scorer));
    RollAll(&scorer, {10U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(300U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A03_should_every_frame_3_3_4_scores_100_ten_ten_boxes)
{
    Scorer scorer = MakeCandlepin();
    for (int frame = 0; frame < 10; frame++) {
        RollAll(&scorer, {3U, 3U, 4U});
    }
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(100U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A04_should_a_spare_in_two_balls_then_3_4_2_scores_22)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {5U, 5U, 3U, 4U, 2U});
    RollMany(&scorer, 24, 0U);
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(22U, Scorer_Score(&scorer)); /* (10 + 3) + (3 + 4 + 2) */
}

TEST(CandlepinTest, A05_should_a_strike_then_3_4_2_scores_26)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {10U, 3U, 4U, 2U});
    RollMany(&scorer, 24, 0U);
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(26U, Scorer_Score(&scorer)); /* (10 + 3 + 4) + (3 + 4 + 2) */
}

TEST(CandlepinTest, A06_should_a_strike_a_spare_and_a_ten_box_score_42)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {10U, 7U, 3U, 2U, 3U, 5U});
    RollMany(&scorer, 21, 0U);
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(42U, Scorer_Score(&scorer)); /* (10 + 7 + 3) + (10 + 2) + 10 */
}

TEST(CandlepinTest, A07_should_three_strikes_in_the_tenth_score_30)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {10U, 10U, 10U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(30U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A08_should_a_spare_in_the_tenth_takes_one_fill_ball_6_4_5_scores_15)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {6U, 4U, 5U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(15U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A09_should_a_strike_in_the_tenth_then_6_4_scores_20)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {10U, 6U, 4U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(20U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A10_should_a_ten_box_in_the_tenth_scores_10_and_gets_no_fill_ball)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {3U, 3U, 4U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(10U, Scorer_Score(&scorer));
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_GAME_OVER, Scorer_Roll(&scorer, 0U, &events)); /* no fill ball */
}

TEST(CandlepinTest, A11_should_an_open_tenth_3_3_3_scores_9)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {3U, 3U, 3U});
    EXPECT_TRUE(Scorer_IsOver(&scorer));
    EXPECT_EQ(9U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A12_should_6_then_5_is_rejected_too_many_pins_and_changes_nothing)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {6U});
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Roll(&scorer, 5U, &events));
    EXPECT_EQ(4U, Scorer_PinsStanding(&scorer));
    EXPECT_EQ(1U, Scorer_FramesStarted(&scorer));
}

TEST(CandlepinTest, A13_should_a_strike_in_the_tenth_then_6_then_5_is_rejected_too_many_pins)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 27, 0U);
    RollAll(&scorer, {10U, 6U});
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Roll(&scorer, 5U, &events)); /* 4 standing */
    EXPECT_FALSE(Scorer_IsOver(&scorer));
}

TEST(CandlepinTest, A14_should_after_an_open_3_3_3_a_10_is_frame_2s_strike)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {3U, 3U, 3U, 10U});
    EXPECT_EQ(2U, Scorer_FramesStarted(&scorer));
    EXPECT_EQ(9U, Scorer_Frame(&scorer, 0U).score);
    EXPECT_FALSE(Scorer_Frame(&scorer, 1U).complete); /* the strike waits for its bonus */
}

TEST(CandlepinTest, A15_should_a_spare_ends_frame_1_after_two_balls_with_a_strike_next_frame_1_is_20)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {5U, 5U, 10U});
    EXPECT_EQ(2U, Scorer_FramesStarted(&scorer)); /* the 10 is frame 2s first ball */
    const ScorerFrame first = Scorer_Frame(&scorer, 0U);
    EXPECT_TRUE(first.complete);
    EXPECT_EQ(20U, first.score);
}

TEST(CandlepinTest, A16_should_a_ball_after_thirty_zeros_is_rejected_the_game_is_over)
{
    Scorer scorer = MakeCandlepin();
    RollMany(&scorer, 30, 0U);
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_GAME_OVER, Scorer_Roll(&scorer, 0U, &events));
    EXPECT_EQ(0U, events.count);
}

TEST(CandlepinTest, A17_should_a_strike_alone_totals_0_with_frame_1_unknown)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {10U});
    EXPECT_EQ(0U, Scorer_Score(&scorer));
    EXPECT_FALSE(Scorer_Frame(&scorer, 0U).complete);
}

TEST(CandlepinTest, A18_should_a_strike_and_one_bonus_ball_total_0_with_frame_1_unknown)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {10U, 3U});
    EXPECT_EQ(0U, Scorer_Score(&scorer));
    EXPECT_FALSE(Scorer_Frame(&scorer, 0U).complete);
}

TEST(CandlepinTest, A19_should_a_strike_and_both_bonus_balls_frame_1_is_known_at_17_and_the_total_is_17)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {10U, 3U, 4U});
    const ScorerFrame first = Scorer_Frame(&scorer, 0U);
    EXPECT_TRUE(first.complete);
    EXPECT_EQ(17U, first.score);
    EXPECT_EQ(17U, Scorer_Score(&scorer)); /* frame 2, 3 and 4, still takes a third ball */
}

TEST(CandlepinTest, A20_should_a_spare_corrected_to_5_then_2_makes_a_ten_box_of_frame_1_and_an_open_6_of_frame)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {5U, 5U, 3U, 4U, 2U});
    RollMany(&scorer, 24, 0U);
    const std::vector<Pins> two = {2U};
    const RollEdit edit = Replace(2U, 1U, two); /* 5, 2, 3 | 4, 2, 0 | ... */
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(10U, Scorer_Frame(&scorer, 0U).score); /* a ten-box */
    EXPECT_EQ(6U, Scorer_Frame(&scorer, 1U).score);  /* open */
    EXPECT_EQ(16U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A21_should_a_correction_that_makes_frame_1_twelve_pins_is_rejected_and_changes_nothing)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {5U, 5U, 3U, 4U, 2U});
    RollMany(&scorer, 24, 0U);
    const std::vector<Pins> four = {4U};
    const RollEdit edit = Replace(2U, 1U, four); /* 5, 4, then 3 with one pin standing */
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(0U, events.count);
    EXPECT_EQ(22U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, A22_should_inserting_a_31st_ball_is_rejected_and_changes_nothing)
{
    Scorer scorer = MakeCandlepin();
    for (int frame = 0; frame < 10; frame++) {
        RollAll(&scorer, {3U, 3U, 4U});
    }
    const std::vector<Pins> one_more = {0U};
    const RollEdit edit = Replace(1U, 0U, one_more);
    FrameEvents events;
    EXPECT_EQ(GAME_ERR_TOO_MANY_ROLLS, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(100U, Scorer_Score(&scorer));
}

TEST(CandlepinTest, should_accept_an_edit_that_leaves_exactly_thirty_balls)
{
    /* The other side of A22: 30 is a whole game, and an edit that keeps it at 30 is fine. */
    Scorer scorer = MakeCandlepin();
    for (int frame = 0; frame < 10; frame++) {
        RollAll(&scorer, {3U, 3U, 4U});
    }
    const std::vector<Pins> two = {2U};
    const RollEdit edit = Replace(1U, 1U, two); /* 2, 3, 4 | ...: frame 1 now open, 9 */
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(99U, Scorer_Score(&scorer));
}

/* ---- Candlepin against its independent reference ------------------------------------- */

TEST(CandlepinTest, should_score_ten_thousand_random_games_as_the_reference_does)
{
    /* Random legal games of random length, a quarter of the balls clearing the rack, checked
     * after every ball: the score, whether the game is over, and the ball after it refused. */
    std::mt19937 random(20260929U);
    for (int game_number = 0; game_number < 10000; ++game_number) {
        Scorer scorer = MakeCandlepin();
        candlepin_reference::Lane lane;
        std::vector<int> balls;
        const int stop_after = std::uniform_int_distribution<int>(1, 30)(random);
        for (int ball = 0; (ball < stop_after) && !lane.over; ++ball) {
            const bool clear_the_rack = std::uniform_int_distribution<int>(0, 3)(random) == 0;
            const int down =
                clear_the_rack ? lane.standing
                               : std::uniform_int_distribution<int>(0, lane.standing)(random);
            RollAll(&scorer, {static_cast<Pins>(down)});
            balls.push_back(down);
            lane.Roll(down);
            ASSERT_EQ(candlepin_reference::Score(balls), static_cast<int>(Scorer_Score(&scorer)))
                << "game " << game_number << ", after ball " << (ball + 1);
            ASSERT_EQ(lane.over, Scorer_IsOver(&scorer)) << "game " << game_number;
        }
        FrameEvents events;
        if (lane.over) {
            ASSERT_EQ(GAME_ERR_GAME_OVER, Scorer_Roll(&scorer, 0U, &events))
                << "game " << game_number;
        }
    }
}

TEST(TenPinScorerQueryTest, should_report_a_frame_not_yet_started_as_not_complete_and_0)
{
    /* Defined for any frame, not only those started: no frame is read that the walk didn't
     * write. */
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {3U, 4U});
    for (uint8_t index = 1U; index < 12U; index++) {
        const ScorerFrame frame = Scorer_Frame(&scorer, index);
        EXPECT_FALSE(frame.complete) << "frame index " << +index;
        EXPECT_EQ(0U, frame.score) << "frame index " << +index;
    }
}

/* ---- Frames complete oldest first ------------------------------------------------------- */

namespace {

/* After every ball of random legal games: no frame is complete after one that isn't. */
template <typename ReferenceLane>
void ExpectCompleteFramesToComeFirst(ScorerVariant variant, int max_balls, unsigned seed)
{
    std::mt19937 random(seed);
    for (int game_number = 0; game_number < 2000; ++game_number) {
        Scorer scorer = MakeScorer(variant);
        ReferenceLane lane;
        for (int ball = 0; (ball < max_balls) && !lane.over; ++ball) {
            const bool clear_the_rack = std::uniform_int_distribution<int>(0, 3)(random) == 0;
            const int down =
                clear_the_rack ? lane.standing
                               : std::uniform_int_distribution<int>(0, lane.standing)(random);
            RollAll(&scorer, {static_cast<Pins>(down)});
            lane.Roll(down);
            bool incomplete_seen = false;
            for (uint8_t index = 0U; index < SCORER_MAX_FRAMES; index++) {
                const bool complete = Scorer_Frame(&scorer, index).complete;
                ASSERT_FALSE(incomplete_seen && complete)
                    << "game " << game_number << ", after ball " << (ball + 1) << ", frame "
                    << (index + 1) << " is complete after one that isn't";
                incomplete_seen = incomplete_seen || !complete;
            }
        }
    }
}

} // namespace

TEST(TenPinScorerTest, should_complete_frames_oldest_first)
{
    ExpectCompleteFramesToComeFirst<ten_pin_reference::Lane>(SCORER_TEN_PIN, 21, 20261001U);
}

TEST(CandlepinTest, should_complete_frames_oldest_first)
{
    ExpectCompleteFramesToComeFirst<candlepin_reference::Lane>(SCORER_CANDLEPIN, 30, 20261002U);
}

/* ---- Edits ported from the Game facade's tests (correction_test.cpp) --------------------- */

TEST(TenPinScorerEditTest, should_fix_a_strike_that_was_really_9_then_1)
{
    /* Replacing the 10 with one ball can't fix it: 9 then the 3 is too many pins. */
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollAll(&scorer, {10U, 3U, 4U}); /* 17 + 7 = 24 */
    const std::vector<Pins> really = {9U, 1U};
    const RollEdit edit = Replace(1U, 1U, really);
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(20U, Scorer_Score(&scorer)); /* a spare, 9 + 1 + 3, then 3 + 4 */
}

TEST(TenPinScorerEditTest, should_fix_a_tenth_frame_entered_with_a_ball_too_many_in_one_edit)
{
    /* Entered as 10, 0, 0 in the tenth; really 9, 0. As one edit, the report never shows a
     * tenth frame waiting for a fill ball. */
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 18, 0U);
    RollAll(&scorer, {10U, 0U, 0U});
    ASSERT_EQ(10U, Scorer_Score(&scorer));
    const std::vector<Pins> really = {9U, 0U};
    const RollEdit edit = Replace(19U, 3U, really);
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(9U, Scorer_Score(&scorer));
    ASSERT_EQ(10U, events.count);
    EXPECT_EQ((Event{10, 9, true}), EventsOf(events).back());
    EXPECT_TRUE(Scorer_IsOver(&scorer));
}

TEST(TenPinScorerEditTest, should_correct_the_first_and_last_balls_of_the_longest_game)
{
    /* Nine open frames, then a spare and its fill ball: 21 balls. */
    Scorer scorer = MakeScorer(SCORER_TEN_PIN);
    RollMany(&scorer, 18, 1U);
    RollAll(&scorer, {5U, 5U, 5U}); /* 18 + 15 = 33 */
    ASSERT_EQ(33U, Scorer_Score(&scorer));
    const std::vector<Pins> seven = {7U};
    const std::vector<Pins> three = {3U};
    const RollEdit last = Replace(21U, 1U, seven);
    const RollEdit first = Replace(1U, 1U, three);
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &last, &events)); /* the fill ball was a 7 */
    EXPECT_EQ(35U, Scorer_Score(&scorer));
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &first, &events)); /* the first ball was a 3 */
    EXPECT_EQ(37U, Scorer_Score(&scorer));
}

TEST(NoTapScorerTest, should_recount_every_replayed_ball_from_the_pins_that_fell)
{
    Scorer scorer = MakeNoTapScorer();
    RollAll(&scorer, {10U, 9U, 3U}); /* a strike, then a no-tap 9 counted as a strike, then 3 */
    /* Now the 9 is a second ball, at 9 standing: a spare, not a no-tap strike. */
    const std::vector<Pins> one = {1U};
    const RollEdit edit = Replace(1U, 1U, one);
    FrameEvents events;
    EXPECT_EQ(GAME_OK, Scorer_Edit(&scorer, &edit, &events));
    EXPECT_EQ(13U, Scorer_Score(&scorer)); /* spare 1 + 9, plus its bonus 3 */
}

/* ---- Edits against fresh games (ported from the facade's property tests) ----------------- */

namespace {

/* Frame number to score, for the complete frames only. */
std::map<int, int> CompleteFrames(const Scorer &scorer)
{
    std::map<int, int> frames;
    for (uint8_t index = 0U; index < SCORER_MAX_FRAMES; index++) {
        const ScorerFrame frame = Scorer_Frame(&scorer, index);
        if (frame.complete) {
            frames[index + 1] = frame.score;
        }
    }
    return frames;
}

/* What a listener that heard every event knows: the frames before, updated by each event. */
std::map<int, int> AfterEvents(std::map<int, int> frames, const FrameEvents &events)
{
    for (const Event &event : EventsOf(events)) {
        if (event.complete) {
            frames[event.frame] = event.score;
        } else {
            frames.erase(event.frame);
        }
    }
    return frames;
}

/* Random replacements, insertions and deletions in random games. Accepted, the game and what its
 * events tell are those of a fresh game of the edited balls; rejected, nothing changes and
 * nothing is told. An edit that starts past the last ball is refused, even where a fresh game
 * would take the balls: adding balls is a roll. */
void CheckEditsAgainstFreshGames(ScorerVariant variant, CountRule rule, unsigned seed)
{
    std::mt19937 random(seed);
    int accepted_count = 0;
    int rejected_count = 0;
    for (int trial = 0; trial < 3000; trial++) {
        Scorer scorer;
        Scorer_InitWithRule(&scorer, variant, rule);
        std::vector<Pins> balls;
        const int length = 1 + static_cast<int>(random() % SCORER_MAX_BALLS);
        for (int tries = 0; (static_cast<int>(balls.size()) < length) && (tries < 200); tries++) {
            FrameEvents ignored;
            const Pins pins = static_cast<Pins>(random() % 11U);
            if (Scorer_Roll(&scorer, pins, &ignored) == GAME_OK) {
                balls.push_back(pins);
            }
        }

        const auto first = static_cast<size_t>(random() % (balls.size() + 1U)); /* an index */
        const size_t removable = std::min<size_t>(2U, balls.size() - first);
        const auto removed = static_cast<size_t>(random() % (removable + 1U));
        std::vector<Pins> new_pins(random() % 3U);
        for (Pins &pins : new_pins) {
            pins = static_cast<Pins>(random() % 11U);
        }
        std::vector<Pins> edited_balls = balls;
        edited_balls.erase(edited_balls.begin() + static_cast<std::ptrdiff_t>(first),
                           edited_balls.begin() + static_cast<std::ptrdiff_t>(first + removed));
        edited_balls.insert(edited_balls.begin() + static_cast<std::ptrdiff_t>(first),
                            new_pins.begin(), new_pins.end());

        Scorer fresh;
        Scorer_InitWithRule(&fresh, variant, rule);
        bool fresh_accepts_all = true;
        for (const Pins pins : edited_balls) {
            FrameEvents ignored;
            fresh_accepts_all =
                fresh_accepts_all && (Scorer_Roll(&fresh, pins, &ignored) == GAME_OK);
        }

        const std::map<int, int> frames_before = CompleteFrames(scorer);
        const Score score_before = Scorer_Score(&scorer);
        const RollEdit edit = Replace(static_cast<RollNumber>(first + 1U),
                                      static_cast<uint8_t>(removed), new_pins);
        FrameEvents events;
        const bool accepted = Scorer_Edit(&scorer, &edit, &events) == GAME_OK;
        ASSERT_EQ((first < balls.size()) && fresh_accepts_all, accepted) << "trial " << trial;

        if (accepted) {
            accepted_count++;
            ASSERT_EQ(CompleteFrames(fresh), CompleteFrames(scorer)) << "trial " << trial;
            ASSERT_EQ(CompleteFrames(fresh), AfterEvents(frames_before, events))
                << "trial " << trial;
            ASSERT_EQ(Scorer_Score(&fresh), Scorer_Score(&scorer)) << "trial " << trial;
            ASSERT_EQ(Scorer_IsOver(&fresh), Scorer_IsOver(&scorer)) << "trial " << trial;
        } else {
            rejected_count++;
            ASSERT_EQ(0U, events.count) << "trial " << trial;
            ASSERT_EQ(frames_before, CompleteFrames(scorer)) << "trial " << trial;
            ASSERT_EQ(score_before, Scorer_Score(&scorer)) << "trial " << trial;
            ASSERT_EQ(balls.size(), Scorer_BallCount(&scorer)) << "trial " << trial;
        }
    }
    EXPECT_GT(accepted_count, 500);
    EXPECT_GT(rejected_count, 500);
}

} // namespace

TEST(TenPinScorerEditTest, should_leave_the_game_as_a_fresh_game_of_the_edited_balls_would)
{
    CheckEditsAgainstFreshGames(SCORER_TEN_PIN, SCORER_COUNT_PINS_DOWN, 20260928U);
}

TEST(NoTapScorerTest, should_leave_the_game_as_a_fresh_game_of_the_edited_balls_would)
{
    CheckEditsAgainstFreshGames(SCORER_TEN_PIN, SCORER_COUNT_NO_TAP, 20260929U);
}

TEST(CandlepinTest, should_leave_the_game_as_a_fresh_game_of_the_edited_balls_would)
{
    CheckEditsAgainstFreshGames(SCORER_CANDLEPIN, SCORER_COUNT_PINS_DOWN, 20260930U);
}
