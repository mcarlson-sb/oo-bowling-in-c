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
     * So the pinsetter counts them, and the main loop can read the count. Reading it changes
     * nothing. */
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    EXPECT_EQ(0U, Pinsetter_RollsLost(pinsetter));

    for (int i = 0; i < 21; i++) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U));
    }
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 1U));
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 2U));
    EXPECT_EQ(2U, Pinsetter_RollsLost(pinsetter));
    EXPECT_EQ(2U, Pinsetter_RollsLost(pinsetter)); /* asking again changes nothing */

    EXPECT_FALSE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_EQ(3U, Pinsetter_RollsLost(pinsetter));
}

TEST(PinsetterTest, should_let_two_readers_each_hear_about_every_lost_roll)
{
    /* A scoreboard and a logger both watch for lost rolls. Asking must not change the answer
     * the other one gets: each keeps its own last value, and takes its own difference. */
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    for (int i = 0; i < 21; i++) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U));
    }
    uint16_t scoreboard_seen = 0U;
    uint16_t logger_seen = 0U;

    EXPECT_FALSE(Pinsetter_Post(pinsetter, 1U));
    EXPECT_FALSE(Pinsetter_Post(pinsetter, 2U));
    EXPECT_EQ(2U, static_cast<uint16_t>(Pinsetter_RollsLost(pinsetter) - scoreboard_seen));
    scoreboard_seen = Pinsetter_RollsLost(pinsetter);
    EXPECT_EQ(2U, static_cast<uint16_t>(Pinsetter_RollsLost(pinsetter) - logger_seen));
    logger_seen = Pinsetter_RollsLost(pinsetter);

    EXPECT_FALSE(Pinsetter_Post(pinsetter, 3U));
    EXPECT_EQ(1U, static_cast<uint16_t>(Pinsetter_RollsLost(pinsetter) - scoreboard_seen));
    EXPECT_EQ(1U, static_cast<uint16_t>(Pinsetter_RollsLost(pinsetter) - logger_seen));
}

TEST(PinsetterTest, should_count_lost_rolls_right_across_the_count_wrapping_around)
{
    /* The count is 16 bits and never cleared, so it wraps: it must go on counting from 0,
     * never stick at 65,535. A reader's difference then stays right across the wrap. */
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    for (int i = 0; i < 21; i++) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, 0U));
    }

    for (int i = 0; i < 65530; i++) {
        (void)Pinsetter_Post(pinsetter, 0U);
    }
    const uint16_t seen = Pinsetter_RollsLost(pinsetter);
    EXPECT_EQ(65530U, seen);

    for (int i = 0; i < 10; i++) { /* the count goes 65,535, then 0, and ends at 4 */
        (void)Pinsetter_Post(pinsetter, 0U);
    }
    EXPECT_EQ(4U, Pinsetter_RollsLost(pinsetter));
    EXPECT_EQ(10U, static_cast<uint16_t>(Pinsetter_RollsLost(pinsetter) - seen));
}

namespace {

/* A fake interrupt handler, fired at a chosen moment: from inside a listener, so while the
 * main loop is in the middle of draining a roll into the game. It posts one roll, and notes
 * what the game's score was just after, to show the post itself never reached the game. */
struct InterruptDuringDrain {
    Game *game = nullptr;
    Pinsetter *pinsetter = nullptr;
    Pins pins = 0U;
    bool fired = false;
    bool posted = false;
    Score score_before = 0U;
    Score score_after = 0U;
};

void InterruptDuringDrain_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                       bool frame_complete)
{
    (void)frame_number;
    (void)frame_score;
    (void)frame_complete;
    auto *interrupt = static_cast<InterruptDuringDrain *>(context);
    if (!interrupt->fired) {
        interrupt->fired = true;
        interrupt->score_before = Game_Score(interrupt->game);
        interrupt->posted = Pinsetter_Post(interrupt->pinsetter, interrupt->pins);
        interrupt->score_after = Game_Score(interrupt->game);
    }
}

} // namespace

TEST(PinsetterTest, should_take_a_roll_posted_by_an_interrupt_in_the_middle_of_a_drain)
{
    /* The interrupt fires as frame 1 completes, mid-drain, with the 6 still waiting. Posting
     * does nothing to the game: the 3 it posts waits behind the 6, and goes in after it. */
    GameHandle game_owner = MakeGame();
    Game *game = game_owner.get();
    PinsetterHandle owner = MakePinsetter();
    Pinsetter *pinsetter = owner.get();
    InterruptDuringDrain interrupt;
    interrupt.game = game;
    interrupt.pinsetter = pinsetter;
    interrupt.pins = 3U;
    ASSERT_TRUE(Game_OnFrameChanged(game, &InterruptDuringDrain_FrameChanged, &interrupt));

    for (const Pins pins : {Pins{3U}, Pins{4U}, Pins{6U}}) {
        EXPECT_TRUE(Pinsetter_Post(pinsetter, pins));
    }
    EXPECT_EQ(GAME_OK, Pinsetter_Drain(pinsetter, game));

    EXPECT_TRUE(interrupt.posted);
    EXPECT_EQ(interrupt.score_before, interrupt.score_after); /* posting touched no game */
    EXPECT_EQ(16U, Game_Score(game)); /* 3+4, then 6+3 */
}

TEST(PinsetterThreadTest, should_hand_every_roll_from_another_thread_to_the_game_in_order)
{
    /* A real thread plays the interrupt handler: it only posts, retrying when the mailbox is
     * full, and counts each refusal. The main loop drains, and starts the next game each time a
     * drain stops at a finished one. Five games of 9 then 1, 21 rolls each, go through one
     * pinsetter: a lost, repeated or reordered roll changes a score. 105 rolls through 22 slots
     * wrap the ring four times, and fill it whenever the main loop falls behind. The main loop
     * reads the lost-roll count while the other thread is still writing it. CI runs this under
     * ThreadSanitizer. */
    constexpr int kRuns = 20;
    constexpr int kGames = 5;
    for (int run = 0; run < kRuns; run++) {
        PinsetterHandle owner = MakePinsetter();
        Pinsetter *pinsetter = owner.get();
        std::atomic<bool> interrupts_done{false};
        int refusals = 0; /* the interrupt thread's own; read only after it has joined */

        std::thread interrupts([pinsetter, &interrupts_done, &refusals] {
            for (int roll = 0; roll < (kGames * 21); roll++) {
                const Pins pins = (((roll % 21) % 2) == 0) ? Pins{9U} : Pins{1U};
                while (!Pinsetter_Post(pinsetter, pins)) {
                    refusals++;
                    std::this_thread::yield(); /* full: wait for the main loop */
                }
            }
            interrupts_done = true;
        });

        GameHandle game_owner = MakeGame();
        int games_over = 0;
        uint16_t lost = 0U;
        const auto drain = [&] {
            while (Pinsetter_Drain(pinsetter, game_owner.get()) == GAME_ERR_GAME_OVER) {
                EXPECT_EQ(190U, Game_Score(game_owner.get())) << "run " << run;
                games_over++;
                game_owner = MakeGame(); /* the next bowler's rolls are waiting for it */
            }
            lost = Pinsetter_RollsLost(pinsetter); /* read while the other thread writes it */
        };
        while (!interrupts_done) {
            drain();
            std::this_thread::yield();
        }
        interrupts.join();
        drain(); /* whatever was posted last */

        ASSERT_EQ(kGames - 1, games_over) << "run " << run; /* no roll refuses the last game */
        ASSERT_EQ(190U, Game_Score(game_owner.get())) << "run " << run;
        ASSERT_EQ(static_cast<uint16_t>(refusals), lost) << "run " << run;
    }
}
