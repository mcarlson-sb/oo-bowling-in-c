/* The scorer core: a pure, data-driven scorer with no callbacks. Each operation returns a
 * status, and writes the frame changes it caused to a buffer the caller supplies. */

#include <gtest/gtest.h>

#include <initializer_list>
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
    EXPECT_EQ(2U, Scorer_FrameCount(&scorer)); /* frames started */
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
    EXPECT_EQ(1U, Scorer_FrameCount(&scorer));
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
    EXPECT_EQ(2U, Scorer_FrameCount(&scorer));
    EXPECT_EQ(9U, Scorer_Frame(&scorer, 0U).score);
    EXPECT_FALSE(Scorer_Frame(&scorer, 1U).complete); /* the strike waits for its bonus */
}

TEST(CandlepinTest, A15_should_a_spare_ends_frame_1_after_two_balls_with_a_strike_next_frame_1_is_20)
{
    Scorer scorer = MakeCandlepin();
    RollAll(&scorer, {5U, 5U, 10U});
    EXPECT_EQ(2U, Scorer_FrameCount(&scorer)); /* the 10 is frame 2s first ball */
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
