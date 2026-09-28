/* The library against an independent reference, the classic procedural kata in
 * ten_pin_reference.h. */
#include <random>
#include <vector>

#include "ten_pin_reference.h"
#include "test_support.h"

namespace {

/* Rolls random valid games into the library, of random length, with extra strikes and spares,
 * and checks Game_Score against the reference after every roll. The reference counts each roll
 * with the same rule, from the pins standing on its own lane. */
void ExpectScoresMatchTheReference(PinCountRule count_pins, unsigned seed)
{
    std::mt19937 random(seed);
    for (int game_number = 0; game_number < 1000; ++game_number) {
        GameHandle owner = MakeGameWithRule(count_pins);
        Game *game = owner.get();
        ten_pin_reference::Lane lane;
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
            ASSERT_EQ(ten_pin_reference::Score(counted), static_cast<int>(Game_Score(game)))
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
