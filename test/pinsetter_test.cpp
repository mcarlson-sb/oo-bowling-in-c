/* The pinsetter: the one part of the system that runs on another thread (in firmware, an
 * interrupt handler). All it may do is post the pins that fell. The game only ever hears
 * about them on the main thread, when the main loop drains the pinsetter. */

#include "pinsetter.h"

#include "test_support.h"

namespace {

using PinsetterHandle = std::unique_ptr<Pinsetter, decltype(&Pinsetter_Destroy)>;

PinsetterHandle MakePinsetter()
{
    return PinsetterHandle(Pinsetter_Create(), &Pinsetter_Destroy);
}

} // namespace

TEST(PinsetterTest, should_roll_the_posted_pins_into_the_game_in_order_when_drained)
{
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 10U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 4U));
    EXPECT_EQ(0U, Game_Score(game)); /* posting alone tells the game nothing */

    Pinsetter_Drain(pinsetter, game);
    EXPECT_EQ(24U, Game_Score(game)); /* 10 + 3 + 4, then 3 + 4 */
}
