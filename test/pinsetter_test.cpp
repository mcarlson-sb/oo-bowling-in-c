/* The pinsetter: the one part of the system that runs on another thread (in firmware, an
 * interrupt handler). All it may do is post the pins that fell. The game only ever hears
 * about them on the main thread, when the main loop drains the pinsetter. */

#include "pinsetter.h"

#include <atomic>
#include <thread>

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

TEST(PinsetterTest, should_refuse_a_post_when_the_mailbox_is_full)
{
    /* The mailbox holds 8 rolls. A ninth, before the main loop drains, can't overwrite the
     * oldest: the pinsetter is told, and can report the lost roll. */
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 5U));
    for (int i = 0; i < 7; i++) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U));
    }
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 9U));

    Pinsetter_Drain(pinsetter, game);
    EXPECT_EQ(5U, Game_Score(game)); /* the 5 and seven gutter balls; no 9 */
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 9U)); /* drained, there is room again */
}

TEST(PinsetterTest, should_apply_rolls_still_waiting_after_a_correction_made_meanwhile)
{
    /* Only rolls wait in a mailbox. An edit applies at once, to the rolls the game has had,
     * so a roll still waiting lands after the edited history. Had the correction waited
     * behind it instead, the 8 would have been judged against the uncorrected 5 pins down,
     * rejected, and lost. */
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 5U)); /* miscounted: 2 fell */
    Pinsetter_Drain(pinsetter, game);
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 8U)); /* the second ball, still waiting */

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 2U));
    Pinsetter_Drain(pinsetter, game);
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));
    Pinsetter_Drain(pinsetter, game);

    EXPECT_EQ(13U, Game_Score(game)); /* 2 then 8: a spare, with the 3 as its bonus */
}

TEST(PinsetterThreadTest, should_hand_every_roll_from_another_thread_to_the_game_in_order)
{
    /* A real thread plays the interrupt handler: it only posts, retrying while the mailbox
     * is full. The main loop drains until that thread is done. 9 then 1, 21 times over: a
     * lost, repeated or reordered roll changes the score. The mailbox holds 8, so it wraps,
     * and fills up, along the way. CI runs this under ThreadSanitizer. */
    for (int game_number = 0; game_number < 20; game_number++) {
        GameHandle game_owner = MakeGame();
        Game *game = game_owner.get();
        PinsetterHandle owner = MakePinsetter();
        Pinsetter *pinsetter = owner.get();
        std::atomic<bool> interrupts_done{false};

        std::thread interrupts([pinsetter, &interrupts_done] {
            for (int roll = 0; roll < 21; roll++) {
                const Pins pins = ((roll % 2) == 0) ? Pins{9U} : Pins{1U};
                while (!Pinsetter_Post(pinsetter, pins)) {
                    std::this_thread::yield(); /* full: wait for the main loop */
                }
            }
            interrupts_done = true;
        });
        while (!interrupts_done) {
            Pinsetter_Drain(pinsetter, game);
            std::this_thread::yield();
        }
        interrupts.join();
        Pinsetter_Drain(pinsetter, game); /* whatever was posted last */

        ASSERT_EQ(190U, Game_Score(game)) << "game " << game_number;
    }
}
