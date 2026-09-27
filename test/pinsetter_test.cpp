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

TEST(PinsetterTest, should_stop_at_an_impossible_roll_and_keep_it_until_the_scorer_resolves_it)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible. The 8 is the roll
     * that's right. Draining stops there and says why, the 8 and the 3 behind it wait, and
     * once the scorer corrects the first roll, the next drain applies them. */
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 5U)); /* miscounted: 2 fell */
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 8U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));

    EXPECT_EQ(GAME_ERR_INVALID_PINS, Pinsetter_Drain(pinsetter, game));
    EXPECT_EQ(0U, Game_Score(game)); /* frame 1 is still open: the 8 and the 3 are waiting */

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 2U));
    EXPECT_EQ(GAME_OK, Pinsetter_Drain(pinsetter, game));
    EXPECT_EQ(13U, Game_Score(game)); /* 2 then 8, a spare with the 3 as its bonus */
}

TEST(PinsetterTest, should_let_the_scorer_discard_a_roll_that_really_was_a_glitch)
{
    /* 11 pins can't fall: this time the machine is wrong, not an earlier roll. The scorer
     * throws the roll away, and the rolls behind it go in. With nothing waiting, there is
     * nothing to discard. */
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 11U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 4U));
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Pinsetter_Drain(pinsetter, game));

    EXPECT_TRUE(Pinsetter_DiscardOldest(pinsetter));
    EXPECT_EQ(GAME_OK, Pinsetter_Drain(pinsetter, game));
    EXPECT_EQ(7U, Game_Score(game));
    EXPECT_FALSE(Pinsetter_DiscardOldest(pinsetter));
}

TEST(PinsetterTest, should_keep_rolls_made_after_the_game_is_over_for_the_next_game)
{
    /* The next bowler starts before the main loop has moved on to the next game. Their rolls
     * are refused by the finished game, and wait, rather than being lost. */
    GameHandle first_owner = MakeGame();
    Game *first = first_owner.get();
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(first, 0U)); /* a whole game of gutter balls */
    }
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();

    EXPECT_TRUE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 4U));
    EXPECT_EQ(GAME_ERR_GAME_OVER, Pinsetter_Drain(pinsetter, first));

    GameHandle next_owner = MakeGame();
    Game *next = next_owner.get();
    EXPECT_EQ(GAME_OK, Pinsetter_Drain(pinsetter, next));
    EXPECT_EQ(7U, Game_Score(next));
}

TEST(PinsetterTest, should_hold_a_whole_game_of_rolls_while_a_drain_is_stopped)
{
    /* The worst case for a stopped drain: the game is over, the main loop hasn't moved on,
     * and the next bowler bowls a whole game, 21 rolls. None may be lost. The mailbox holds
     * exactly that many: a 22nd roll is refused. */
    GameHandle first_owner = MakeGame();
    Game *first = first_owner.get();
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(GAME_OK, Game_Roll(first, 0U));
    }
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    EXPECT_TRUE(Pinsetter_Post(pinsetter, 9U));
    EXPECT_EQ(GAME_ERR_GAME_OVER, Pinsetter_Drain(pinsetter, first));

    for (int roll = 1; roll < 21; roll++) { /* 9 then 1, ten times over, then a fill 9 */
        EXPECT_TRUE(Pinsetter_Post(pinsetter, ((roll % 2) == 0) ? Pins{9U} : Pins{1U}))
            << "roll " << roll + 1;
    }
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 0U));

    GameHandle next_owner = MakeGame();
    Game *next = next_owner.get();
    EXPECT_EQ(GAME_OK, Pinsetter_Drain(pinsetter, next));
    EXPECT_EQ(190U, Game_Score(next)); /* ten spares, each with a 9 as its bonus */    EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U)); /* drained, there is room again */
}

TEST(PinsetterTest, should_count_the_rolls_lost_while_the_mailbox_was_full)
{
    /* A full mailbox refuses the interrupt handler's roll, and the handler has no one to tell.
     * So the pinsetter counts them, and the main loop asks how many were lost since it last
     * asked. */
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    EXPECT_EQ(0U, Pinsetter_RollsLost(pinsetter));

    for (int i = 0; i < 21; i++) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U));
    }
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 1U));
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 2U));
    EXPECT_EQ(2U, Pinsetter_RollsLost(pinsetter));
    EXPECT_EQ(0U, Pinsetter_RollsLost(pinsetter)); /* none since it last asked */

    EXPECT_FALSE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_EQ(1U, Pinsetter_RollsLost(pinsetter));
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
