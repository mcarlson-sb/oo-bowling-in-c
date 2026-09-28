/* The library against an independent reference: the classic procedural kata, written here and
 * sharing no code with the library. */
#include <random>
#include <vector>

#include "test_support.h"

namespace {

/* Complete frames only, as Game_Score counts them. */
int ReferenceScore(const std::vector<int> &rolls)
{
    int score = 0;
    size_t i = 0;
    for (int frame = 0; frame < 10; ++frame) {
        const size_t left = rolls.size() - i;
        if ((left >= 1) && (rolls[i] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + rolls[i + 1] + rolls[i + 2];
            i += 1;
        } else if ((left >= 2) && (rolls[i] + rolls[i + 1] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + rolls[i + 2];
            i += 2;
        } else {
            if (left < 2) {
                break;
            }
            score += rolls[i] + rolls[i + 1];
            i += 2;
        }
    }
    return score;
}

/* The lane: which frame and ball is next, how many pins stand, and whether the game is over.
 * The tenth frame gets a fresh rack after a strike or a spare, and ends after two balls that
 * leave pins standing, or after three. */
struct Lane {
    int frame = 1;
    int ball = 0;
    int standing = 10;
    int tenth_first_two = 0;
    bool over = false;

    void Roll(int counted)
    {
        standing -= counted;
        ++ball;
        if (frame < 10) {
            if ((standing == 0) || (ball == 2)) {
                ++frame;
                ball = 0;
                standing = 10;
            }
            return;
        }
        if (ball <= 2) {
            tenth_first_two += counted;
        }
        over = (ball == 3) || ((ball == 2) && (tenth_first_two < 10));
        if (standing == 0) {
            standing = 10;
        }
    }
};

/* Rolls random valid games into the library, of random length, with extra strikes and spares,
 * and checks Game_Score against the reference after every roll. The reference counts each roll
 * with the same rule, from the pins standing on its own lane. */
void ExpectScoresMatchTheReference(PinCountRule count_pins, unsigned seed)
{
    std::mt19937 random(seed);
    for (int game_number = 0; game_number < 1000; ++game_number) {
        GameHandle owner = MakeGameWithRule(count_pins);
        Game *game = owner.get();
        Lane lane;
        std::vector<int> counted;
        const int stop_after = std::uniform_int_distribution<int>(1, 21)(random);
        for (int roll = 0; (roll < stop_after) && !lane.over; ++roll) {
            const bool clear_the_rack = std::uniform_int_distribution<int>(0, 3)(random) == 0;
            const int down =
                clear_the_rack ? lane.standing
                               : std::uniform_int_distribution<int>(0, lane.standing)(random);
            const int as_counted = count_pins(static_cast<Pins>(lane.standing),
                                              static_cast<Pins>(down));
            ASSERT_EQ(GAME_OK, Game_Roll(game, static_cast<Pins>(down)));
            counted.push_back(as_counted);
            lane.Roll(as_counted);
            ASSERT_EQ(ReferenceScore(counted), static_cast<int>(Game_Score(game)))
                << "game " << game_number << ", after roll " << (roll + 1);
        }
    }
}

Pins CountPinsDown(Pins pins_standing, Pins pins_down)
{
    (void)pins_standing;
    return pins_down;
}

} // namespace

TEST(ReferenceScorerTest, should_score_random_games_as_the_reference_does)
{
    ExpectScoresMatchTheReference(&CountPinsDown, 1U);
}

TEST(ReferenceScorerTest, should_score_random_no_tap_games_as_the_reference_does)
{
    ExpectScoresMatchTheReference(&NinePinNoTap, 2U);
}
