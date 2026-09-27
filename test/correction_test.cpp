/* A scorer fixing a roll entered wrongly: the game rescores everything after it. */
#include <algorithm>
#include <cstddef>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include "test_support.h"

TEST(CorrectionTest, should_rescore_the_game_when_a_roll_is_corrected)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* the first roll was a 5, not a 3 */
    EXPECT_EQ(9U, Game_Score(game));
}

TEST(CorrectionTest, should_reject_a_correction_that_makes_a_later_roll_impossible)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {7U, 3U, 5U}); /* a spare, 7 + 3, then 5: 15 so far */

    /* Correcting the 7 to an 8 would make the 3 too many pins (8 + 3 > 10). */
    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_CorrectRoll(game, 1U, 8U));
    EXPECT_EQ(15U, Game_Score(game)); /* unchanged */

    RollAll(game, {4U}); /* and the game carries on from where it was */
    EXPECT_EQ(24U, Game_Score(game));
}

TEST(CorrectionTest, should_recount_every_replayed_roll_from_the_pins_that_fell)
{
    /* Predicted in phase 1, two phases before a feature needed it: replaying means applying
     * the caller's rule again, so the game must keep the pins that fell, not what they were
     * counted as. */
    GameHandle owner = MakeGameWithRule(&NinePinNoTap);
    Game *game = owner.get();
    RollAll(game, {10U, 9U, 3U}); /* a strike, then a no-tap 9 counted as a strike, then 3 */

    /* The first roll was really a 1. Now the 9 is a second ball, at 9 pins standing: a
     * spare, not a strike. It must be counted again from the 9 that fell. */
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 1U));
    EXPECT_EQ(13U, Game_Score(game)); /* spare 1 + 9, plus its bonus 3 */
}

/* ---- Telling the listeners about a correction ------------------------------------------- *
 * A correction is told with the same frame-completed message, and a listener treats a frame
 * number it has heard before as an update. */


namespace {

/* A scoreboard that keeps the latest score it has heard for each frame. */
struct KeyedScoreboard {
    std::map<int, int> scores; /* frame number -> score */
};

void KeyedScoreboard_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                  bool frame_complete)
{
    auto *scoreboard = static_cast<KeyedScoreboard *>(context);
    if (frame_complete) {
        scoreboard->scores[frame_number] = frame_score;
    } else {
        scoreboard->scores.erase(frame_number); /* reopened by a correction */
    }
}

} // namespace

TEST(CorrectionListenerTest, should_tell_the_scoreboard_the_rescored_frames)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    KeyedScoreboard scoreboard;
    ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));
    RollAll(game, {3U, 4U, 5U, 2U});
    ASSERT_EQ((std::map<int, int>{{1, 7}, {2, 7}}), scoreboard.scores);

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* frame 1 was 5, 4 */
    EXPECT_EQ((std::map<int, int>{{1, 9}, {2, 7}}), scoreboard.scores);
}

TEST(CorrectionListenerTest, should_keep_running_stats_right_after_a_correction)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
    RollAll(game, {3U, 4U, 5U, 2U}); /* frames of 7 and 7 */

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 5U)); /* frames of 9 and 7 */
    EXPECT_EQ(2, stats.Frames());
    EXPECT_DOUBLE_EQ(8.0, stats.Average());
}

TEST(CorrectionListenerTest, should_tell_the_listeners_when_a_correction_reopens_a_frame)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    KeyedScoreboard scoreboard;
    RunningStats stats;
    ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));
    ASSERT_TRUE(Game_OnFrameChanged(game, &RunningStats_FrameChanged, &stats));
    RollAll(game, {3U, 4U}); /* frame 1 complete: 7 */

    /* The first ball was really a strike: frame 1 now waits for a second bonus roll. */
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 10U));
    EXPECT_EQ(0U, Game_Score(game));
    EXPECT_TRUE(scoreboard.scores.empty());
    EXPECT_EQ(0, stats.Frames());

    RollAll(game, {2U}); /* its second bonus: frame 1 is 16, frame 2 is 6 */
    EXPECT_EQ((std::map<int, int>{{1, 16}, {2, 6}}), scoreboard.scores);
}

/* ---- A correction the game can't make -------------------------------------------------- */

namespace {

/* A rule that breaks the PinCountRule contract on purpose: it counts the pins that fell until
 * the test flips `s_impure_rule_broken`, and from then on counts one more pin than were
 * standing. Every replay after the flip fails at its first roll. */
bool s_impure_rule_broken = false;

Pins ImpureRule(Pins pins_standing, Pins pins_down)
{
    return s_impure_rule_broken ? static_cast<Pins>(pins_standing + 1U) : pins_down;
}

} // namespace

TEST(CorrectionDeathTest, should_stop_the_program_when_a_rejected_edit_cannot_be_undone)
{
    /* Undoing a rejected edit replays the saved rolls, and trusts the rule to count them as it
     * did before. An impure rule breaks that trust: the replay fails too, and there is no game
     * left that is known to be right. So the program stops, in every build, release included.
     * It all happens in the death test's child process. */
    EXPECT_DEATH(
        {
            s_impure_rule_broken = false;
            Game *game = Game_CreateWithRule(&ImpureRule);
            (void)Game_Roll(game, 3U);
            (void)Game_Roll(game, 4U);
            s_impure_rule_broken = true;
            (void)Game_CorrectRoll(game, 1U, 2U); /* rejected, and the restore fails */
        },
        "replay");
}

TEST(CorrectionTest, should_reject_correcting_a_roll_that_has_not_been_made)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 0U, 5U)); /* rolls start at 1 */
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 3U, 5U)); /* only 2 so far */
    EXPECT_EQ(7U, Game_Score(game));
}

TEST(CorrectionTest, should_reject_correcting_a_null_game)
{
    EXPECT_EQ(GAME_ERR_NULL_GAME, Game_CorrectRoll(nullptr, 1U, 5U));
}

namespace {

/* A listener that tries to correct a roll from inside its own notification, once. */
struct CorrectsFromInside {
    Game *game = nullptr;
    bool tried = false;
    GameStatus status = GAME_OK;
};

void CorrectsFromInside_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                                     bool frame_complete)
{
    (void)frame_number;
    (void)frame_score;
    (void)frame_complete;
    auto *listener = static_cast<CorrectsFromInside *>(context);
    if (!listener->tried) {
        listener->tried = true;
        listener->status = Game_CorrectRoll(listener->game, 1U, 5U);
    }
}

} // namespace

TEST(CorrectionTest, should_refuse_a_correction_made_from_inside_a_listener)
{
    /* A replay mid-notification would tell listeners about frames out of order, or twice. So
     * it is refused, like a roll from inside a listener. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    CorrectsFromInside listener;
    listener.game = game;
    ASSERT_TRUE(Game_OnFrameChanged(game, &CorrectsFromInside_FrameChanged, &listener));

    RollAll(game, {3U, 4U});
    EXPECT_EQ(GAME_ERR_DURING_NOTIFICATION, listener.status);
    EXPECT_EQ(7U, Game_Score(game)); /* the refused correction changed nothing */
}

/* ---- A property: a corrected game tells its listeners what a fresh game would ----------- */


namespace {

/* The property, for games made by `make_game`: the standard rule, or a caller's. */
void CheckCorrectionsAgainstFreshGames(GameHandle (*make_game)(), unsigned seed)
{
    /* Random games (fixed seed, so it repeats), each corrected at a random roll. A fresh game,
     * fed the corrected rolls from the start, is the judge:
     *   - The correction must be accepted exactly when the fresh game accepts every corrected
     *     roll. (Without this, a correction wrongly rejected would pass.)
     *   - When it is accepted, the listener must end up exactly where the fresh game's does:
     *     the same frames, the same scores, with any reopened frame dropped.
     *   - When it is rejected, nothing may change. */
    std::mt19937 random(seed);
    int corrections_accepted = 0;
    int corrections_rejected = 0;
    for (int trial = 0; trial < 3000; trial++) {
        GameHandle owner = make_game();
        Game *game = owner.get();
        KeyedScoreboard scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));

        std::vector<Pins> rolls;
        const int length = 1 + static_cast<int>(random() % 21U);
        for (int tries = 0; (static_cast<int>(rolls.size()) < length) && (tries < 200); tries++) {
            const Pins pins = static_cast<Pins>(random() % 11U);
            if (Game_Roll(game, pins) == GAME_OK) {
                rolls.push_back(pins);
            }
        }

        const auto roll_number = static_cast<uint8_t>(1U + random() % rolls.size());
        const Pins corrected = static_cast<Pins>(random() % 11U);
        std::vector<Pins> corrected_rolls = rolls;
        corrected_rolls[roll_number - 1U] = corrected;

        /* The judge: does a fresh game accept every corrected roll? */
        GameHandle fresh_owner = make_game();
        Game *fresh = fresh_owner.get();
        KeyedScoreboard fresh_scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(fresh, &KeyedScoreboard_FrameChanged, &fresh_scoreboard));
        bool fresh_accepts_all = true;
        for (const Pins pins : corrected_rolls) {
            fresh_accepts_all = fresh_accepts_all && (Game_Roll(fresh, pins) == GAME_OK);
        }

        const std::map<int, int> scores_before = scoreboard.scores;
        const Score score_before = Game_Score(game);
        const bool accepted = (Game_CorrectRoll(game, roll_number, corrected) == GAME_OK);
        ASSERT_EQ(fresh_accepts_all, accepted) << "trial " << trial;

        if (accepted) {
            corrections_accepted++;
            ASSERT_EQ(fresh_scoreboard.scores, scoreboard.scores) << "trial " << trial;
            ASSERT_EQ(Game_Score(fresh), Game_Score(game)) << "trial " << trial;
        } else {
            corrections_rejected++;
            ASSERT_EQ(scores_before, scoreboard.scores) << "trial " << trial;
            ASSERT_EQ(score_before, Game_Score(game)) << "trial " << trial;
        }
    }
    EXPECT_GT(corrections_accepted, 500); /* every part of the property was really */
    EXPECT_GT(corrections_rejected, 500); /* exercised */
}

} // namespace

TEST(CorrectionPropertyTest, should_leave_listeners_as_a_fresh_game_of_the_corrected_rolls_would)
{
    CheckCorrectionsAgainstFreshGames(&MakeGame, 20260926U);
}

TEST(CorrectionPropertyTest, should_hold_under_the_no_tap_rule_too)
{
    /* Every replay counts every roll again through the caller's rule. Under no-tap, a 9 on
     * a full rack is a strike and a 9 anywhere else isn't, so a correction that moves a
     * rack's boundary changes how later rolls count. The fresh game must agree anyway. */
    CheckCorrectionsAgainstFreshGames([] { return MakeGameWithRule(&NinePinNoTap); }, 20260927U);
}

/* ---- After review ----------------------------------------------------------------------- */

TEST(CorrectionListenerTest, should_tell_the_listeners_nothing_when_a_correction_is_rejected)
{
    /* A rejected correction replays the corrected log, fails partway, then replays the
     * original. Its net effect is nothing, so the listeners must hear nothing at all. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {7U, 3U, 5U, 2U}); /* frames of 15 and 7 */

    std::vector<std::pair<int, int>> heard;
    auto record = [](void *context, uint8_t frame_number, Score frame_score, bool frame_complete) {
        static_cast<std::vector<std::pair<int, int>> *>(context)->emplace_back(
            frame_complete ? frame_number : -frame_number, frame_score);
    };
    ASSERT_TRUE(Game_OnFrameChanged(game, record, &heard));
    /* Added mid-game, the listener is caught up first; the test is about what comes after. */
    EXPECT_EQ((std::vector<std::pair<int, int>>{{1, 15}, {2, 7}}), heard);
    heard.clear();

    EXPECT_EQ(GAME_ERR_INVALID_PINS, Game_CorrectRoll(game, 1U, 8U)); /* 8 + 3 > 10 */
    EXPECT_TRUE(heard.empty());
    EXPECT_EQ(22U, Game_Score(game));
}

TEST(CorrectionTest, should_correct_the_first_and_last_rolls_of_the_longest_game)
{
    /* The longest game there is: nine open frames, then a spare and its fill ball in the
     * tenth, 21 rolls. Correcting roll 1 and roll 21 covers both ends of the log. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    for (int frame = 1; frame <= 9; frame++) {
        RollAll(game, {1U, 1U});
    }
    RollAll(game, {5U, 5U, 5U}); /* 18 + 15 = 33 */
    ASSERT_EQ(33U, Game_Score(game));

    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 21U, 7U)); /* the fill ball was a 7 */
    EXPECT_EQ(35U, Game_Score(game));
    EXPECT_EQ(GAME_OK, Game_CorrectRoll(game, 1U, 3U)); /* the very first ball was a 3 */
    EXPECT_EQ(37U, Game_Score(game));
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_CorrectRoll(game, 22U, 1U)); /* no 22nd roll */
    EXPECT_EQ(GAME_ERR_GAME_OVER, Game_Roll(game, 1U)); /* still over after correcting */
}

/* ---- Editing a range of rolls ----------------------------------------------------------- */

namespace {

/* Every message a listener hears, in order. */
struct Transcript {
    struct Message {
        int frame;
        int score;
        bool complete;
        bool operator==(const Message &other) const
        {
            return (frame == other.frame) && (score == other.score) && (complete == other.complete);
        }
    };
    std::vector<Message> messages;

    std::vector<Message> AboutFrame(int frame) const
    {
        std::vector<Message> about;
        for (const Message &message : messages) {
            if (message.frame == frame) {
                about.push_back(message);
            }
        }
        return about;
    }
};

void Transcript_FrameChanged(void *context, uint8_t frame_number, Score frame_score,
                             bool frame_complete)
{
    auto *transcript = static_cast<Transcript *>(context);
    transcript->messages.push_back({frame_number, frame_score, frame_complete});
}

} // namespace

TEST(EditRollsTest, should_fix_a_tenth_frame_entered_with_a_roll_too_many_in_one_edit)
{
    /* Entered as 10, 0, 0 in the tenth (a strike and two fill balls), but it was really 9, 0.
     * The fix changes one roll and removes another. Done as one edit, it is checked, and told
     * to the listeners, only in its final state: they never hear about a tenth frame waiting
     * for a fill ball, a game that never existed. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    for (int i = 0; i < 18; i++) {
        ASSERT_EQ(GAME_OK, Game_Roll(game, 0U));
    }
    RollAll(game, {10U, 0U, 0U});
    ASSERT_EQ(10U, Game_Score(game));
    Transcript transcript;
    ASSERT_TRUE(Game_OnFrameChanged(game, &Transcript_FrameChanged, &transcript));
    /* Added mid-game, the listener is caught up first; the test is about what comes after. */
    EXPECT_EQ((std::vector<Transcript::Message>{{10, 10, true}}), transcript.AboutFrame(10));
    transcript.messages.clear();

    const Pins really[] = {9U, 0U};
    const RollEdit edit = MakeEdit(19U, 3U, really, 2U); /* rolls 19 to 21 become 9, 0 */
    EXPECT_EQ(GAME_OK, Game_EditRolls(game, &edit));

    EXPECT_EQ(9U, Game_Score(game));
    EXPECT_EQ((std::vector<Transcript::Message>{{10, 9, true}}), transcript.AboutFrame(10));
}

TEST(EditRollsTest, should_fix_a_strike_that_was_really_9_then_1)
{
    /* The scorer's most likely mistake: a 10 entered for 9 then 1. Replacing one roll can't
     * fix it (9 then the 3 would be too many pins); replacing it with two rolls can. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {10U, 3U, 4U}); /* 17 + 7 = 24 */

    const Pins really[] = {9U, 1U};
    const RollEdit edit = MakeEdit(1U, 1U, really, 2U);
    EXPECT_EQ(GAME_OK, Game_EditRolls(game, &edit));
    EXPECT_EQ(20U, Game_Score(game)); /* a spare, 9 + 1 + 3, then 3 + 4 */
}

TEST(EditRollsTest, should_reject_an_edit_that_would_make_more_rolls_than_a_game_can_have)
{
    /* The longest game, 21 rolls. Inserting one more can't be a real game, and the edited log
     * has room for 21: it must be refused before anything is written. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    for (int frame = 1; frame <= 9; frame++) {
        RollAll(game, {1U, 1U});
    }
    RollAll(game, {5U, 5U, 5U});

    const Pins extra[] = {1U};
    const RollEdit edit = MakeEdit(1U, 0U, extra, 1U);
    EXPECT_EQ(GAME_ERR_TOO_MANY_ROLLS, Game_EditRolls(game, &edit));
    EXPECT_EQ(33U, Game_Score(game));
}

TEST(EditRollsTest, should_reject_an_edit_that_starts_past_the_last_roll)
{
    /* An edit changes rolls the game has had. Inserting just past the last one would be a
     * second way to roll, reported to the listeners as a correction. A forgotten last roll is
     * a Game_Roll. */
    GameHandle empty_owner = MakeGame();
    Game *empty = empty_owner.get();
    const Pins new_pins[] = {3U, 4U};
    const RollEdit at_roll_1 = MakeEdit(1U, 0U, new_pins, 2U);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_EditRolls(empty, &at_roll_1));
    EXPECT_EQ(0U, Game_Score(empty));

    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});
    const RollEdit at_roll_3 = MakeEdit(3U, 0U, new_pins, 2U);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_EditRolls(game, &at_roll_3));
    EXPECT_EQ(7U, Game_Score(game));
    RollAll(game, {3U, 4U}); /* the game is still where it was */
    EXPECT_EQ(14U, Game_Score(game));
}

TEST(EditRollsTest, should_refuse_new_rolls_given_as_a_null_pointer)
{
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    const RollEdit edit = MakeEdit(1U, 1U, nullptr, 1U);
    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_EditRolls(game, &edit));
    EXPECT_EQ(7U, Game_Score(game));
}

TEST(EditRollsTest, should_refuse_an_edit_given_as_a_null_pointer)
{
    /* No edit at all is refused the same way as new rolls promised but not given, and changes
     * nothing. */
    GameHandle owner = MakeGame();
    Game *game = owner.get();
    RollAll(game, {3U, 4U});

    EXPECT_EQ(GAME_ERR_NO_SUCH_ROLL, Game_EditRolls(game, nullptr));
    EXPECT_EQ(7U, Game_Score(game));
    RollAll(game, {3U, 4U}); /* the game is still where it was */
    EXPECT_EQ(14U, Game_Score(game));
}

namespace {

/* The same property for any edit: a random range of rolls (possibly none) replaced by up to
 * two random rolls (possibly none), so replacing, inserting and deleting are all covered,
 * including deleting every roll. A fresh game, fed the edited rolls, is the judge, as for
 * corrections, with one rule of its own: an edit starting past the last roll is rejected
 * even when the fresh game would accept it, because adding a roll is Game_Roll's job. */
void CheckEditsAgainstFreshGames(GameHandle (*make_game)(), unsigned seed)
{
    std::mt19937 random(seed);
    int edits_accepted = 0;
    int edits_rejected = 0;
    for (int trial = 0; trial < 3000; trial++) {
        GameHandle owner = make_game();
        Game *game = owner.get();
        KeyedScoreboard scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(game, &KeyedScoreboard_FrameChanged, &scoreboard));

        std::vector<Pins> rolls;
        const int length = 1 + static_cast<int>(random() % 21U);
        for (int tries = 0; (static_cast<int>(rolls.size()) < length) && (tries < 200); tries++) {
            const Pins pins = static_cast<Pins>(random() % 11U);
            if (Game_Roll(game, pins) == GAME_OK) {
                rolls.push_back(pins);
            }
        }

        const auto first = static_cast<size_t>(random() % (rolls.size() + 1U)); /* index */
        const size_t removable = std::min<size_t>(2U, rolls.size() - first);
        const auto removed = static_cast<size_t>(random() % (removable + 1U));
        std::vector<Pins> new_pins(random() % 3U);
        for (Pins &pins : new_pins) {
            pins = static_cast<Pins>(random() % 11U);
        }
        std::vector<Pins> edited_rolls = rolls;
        edited_rolls.erase(edited_rolls.begin() + static_cast<std::ptrdiff_t>(first),
                           edited_rolls.begin() + static_cast<std::ptrdiff_t>(first + removed));
        edited_rolls.insert(edited_rolls.begin() + static_cast<std::ptrdiff_t>(first),
                            new_pins.begin(), new_pins.end());

        GameHandle fresh_owner = make_game();
        Game *fresh = fresh_owner.get();
        KeyedScoreboard fresh_scoreboard;
        ASSERT_TRUE(Game_OnFrameChanged(fresh, &KeyedScoreboard_FrameChanged, &fresh_scoreboard));
        bool fresh_accepts_all = true;
        for (const Pins pins : edited_rolls) {
            fresh_accepts_all = fresh_accepts_all && (Game_Roll(fresh, pins) == GAME_OK);
        }

        const std::map<int, int> scores_before = scoreboard.scores;
        const Score score_before = Game_Score(game);
        const RollEdit edit =
            MakeEdit(static_cast<RollNumber>(first + 1U), static_cast<uint8_t>(removed),
                     new_pins.data(), static_cast<uint8_t>(new_pins.size()));
        const bool accepted = Game_EditRolls(game, &edit) == GAME_OK;
        const bool starts_at_a_roll = first < rolls.size();
        ASSERT_EQ(starts_at_a_roll && fresh_accepts_all, accepted) << "trial " << trial;

        if (accepted) {
            edits_accepted++;
            ASSERT_EQ(fresh_scoreboard.scores, scoreboard.scores) << "trial " << trial;
            ASSERT_EQ(Game_Score(fresh), Game_Score(game)) << "trial " << trial;
        } else {
            edits_rejected++;
            ASSERT_EQ(scores_before, scoreboard.scores) << "trial " << trial;
            ASSERT_EQ(score_before, Game_Score(game)) << "trial " << trial;
        }
    }
    EXPECT_GT(edits_accepted, 500);
    EXPECT_GT(edits_rejected, 500);
}

} // namespace

TEST(EditRollsPropertyTest, should_leave_listeners_as_a_fresh_game_of_the_edited_rolls_would)
{
    CheckEditsAgainstFreshGames(&MakeGame, 20260928U);
}

TEST(EditRollsPropertyTest, should_hold_under_the_no_tap_rule_too)
{
    CheckEditsAgainstFreshGames([] { return MakeGameWithRule(&NinePinNoTap); }, 20260929U);
}
