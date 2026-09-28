#include "game.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "fault.h"
#include "frame_listeners.h"
#include "roll_log.h"
#include "scorecard.h"
#include "slot_pool.h"

#define GAME_POOL_SIZE 2U

struct Game {
    Scorecard scorecard;
    PinCountRule count_pins;
    uint8_t frames_told_complete;
    FrameListeners listeners;
    bool busy; /* see Game_Roll */
    RollLog log;
};

static Game s_games[GAME_POOL_SIZE];
static bool s_in_use[GAME_POOL_SIZE];
static SlotPool s_pool = { s_in_use, GAME_POOL_SIZE };

static inline FrameNumber FrameNumber_FromIndex(uint8_t index)
{
    return (FrameNumber)(index + 1U);
}

/* Tells the listeners about the frames from index `first` on, and about a frame they were told
 * was complete, and no longer is, as reopened. Inline, like the two below, because every
 * callback's stack sits on top of it. */
static inline void Game_ReportFrames(Game *game, uint8_t first, uint8_t were_told_complete)
{
    const Scorecard *scorecard = &game->scorecard;
    const uint8_t frame_count = Scorecard_FrameCount(scorecard);
    game->frames_told_complete = first;
    const uint8_t frames = (were_told_complete > frame_count) ? were_told_complete : frame_count;
    for (uint8_t i = first; i < frames; i++) {
        const FrameNumber frame_number = FrameNumber_FromIndex(i);
        if ((i < frame_count) && Scorecard_IsFrameComplete(scorecard, i)) {
            FrameListeners_Tell(&game->listeners, frame_number, Scorecard_FrameScore(scorecard, i),
                                true);
            game->frames_told_complete = frame_number;
        } else if (i < were_told_complete) {
            FrameListeners_Tell(&game->listeners, frame_number, 0U, false);
        }
    }
    assert(Scorecard_AllFramesCompleteBefore(scorecard, game->frames_told_complete));
}

/* A roll can't change or reopen a frame already reported. */
static inline void Game_ReportAfterRoll(Game *game)
{
    Game_ReportFrames(game, game->frames_told_complete, game->frames_told_complete);
}

static inline void Game_ReportAfterEdit(Game *game, uint8_t were_told_complete)
{
    Game_ReportFrames(game, 0U, were_told_complete);
}

static Pins Game_CountPinsDown(Pins pins_standing, Pins pins_down)
{
    (void)pins_standing;
    return pins_down;
}

Game *Game_Create(void)
{
    return Game_CreateWithRule(&Game_CountPinsDown);
}

Game *Game_CreateWithRule(PinCountRule count_pins)
{
    if (count_pins == NULL) {
        return NULL;
    }
    uint8_t slot = 0U;
    if (!SlotPool_Acquire(&s_pool, &slot)) {
        return NULL;
    }
    Game *game = &s_games[slot];
    Scorecard_Init(&game->scorecard);
    game->count_pins = count_pins;
    game->frames_told_complete = 0U;
    FrameListeners_Init(&game->listeners);
    game->busy = false;
    RollLog_Init(&game->log);
    return game;
}

static bool Game_FindSlot(const Game *game, uint8_t *slot)
{
    for (uint8_t i = 0U; i < GAME_POOL_SIZE; i++) {
        if (&s_games[i] == game) {
            *slot = i;
            return true;
        }
    }
    return false;
}

/* A busy destroy stops rather than being ignored: the leaked slot would surface later, far
 * away, as Game_Create returning NULL. */
void Game_Destroy(Game *game)
{
    uint8_t slot = 0U;
    if (!Game_FindSlot(game, &slot)) {
        return;
    }
    if (game->busy) {
        Fault_Stop("game: destroyed while busy, from inside its own callback or rule");
    }
    if (!SlotPool_Release(&s_pool, slot)) {
        Fault_Stop("game: destroyed twice");
    }
}

/* Tells no one. Every check comes before any frame sees the roll: frames can't undo one. */
static GameStatus Game_Accept(Game *game, Pins pins)
{
    if (Scorecard_IsOver(&game->scorecard)) {
        return GAME_ERR_GAME_OVER;
    }
    const Pins pins_standing = Scorecard_PinsStanding(&game->scorecard);
    if (pins > pins_standing) {
        return GAME_ERR_INVALID_PINS;
    }

    const Pins pins_counted = game->count_pins(pins_standing, pins);
    if (pins_counted > pins_standing) {
        return GAME_ERR_RULE_OUT_OF_RANGE;
    }
    Scorecard_Roll(&game->scorecard, pins_counted);
    return GAME_OK;
}

/* Busy for the whole roll or edit, since the rule and the listeners run inside it: a change
 * from there would land under the one in progress, and tell frames out of order. */
GameStatus Game_Roll(Game *game, Pins pins)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->busy) {
        return GAME_ERR_BUSY;
    }
    game->busy = true;
    const GameStatus status = Game_Accept(game, pins);
    if (status == GAME_OK) {
        RollLog_Append(&game->log, pins);
        Game_ReportAfterRoll(game);
    }
    game->busy = false;
    return status;
}

Score Game_Score(const Game *game)
{
    if (game == NULL) {
        return 0U;
    }
    return Scorecard_Score(&game->scorecard);
}

/* Only the newest: the others would take a repeat for an update. */
static void Game_CatchUpNewestListener(Game *game)
{
    game->busy = true;
    for (uint8_t i = 0U; i < game->frames_told_complete; i++) {
        FrameListeners_TellNewest(&game->listeners, FrameNumber_FromIndex(i),
                                  Scorecard_FrameScore(&game->scorecard, i), true);
    }
    game->busy = false;
}

bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context)
{
    if ((game == NULL) || game->busy) {
        return false;
    }
    if (!FrameListeners_Add(&game->listeners, callback, context)) {
        return false;
    }
    Game_CatchUpNewestListener(game);
    return true;
}

/* Stops at the first roll that can't happen. */
static GameStatus Game_Replay(Game *game)
{
    Scorecard_Init(&game->scorecard);
    for (uint8_t i = 0U; i < RollLog_Count(&game->log); i++) {
        const GameStatus status = Game_Accept(game, RollLog_At(&game->log, i));
        if (status != GAME_OK) {
            return status;
        }
    }
    return GAME_OK;
}

/* A rejected edit puts the saved log back and replays it. That replay fails only if the rule
 * isn't pure, and then no known-good game is left, so the program stops. */
static GameStatus Game_ApplyEditedLog(Game *game, const RollLog *edited)
{
    const uint8_t were_told_complete = game->frames_told_complete;
    const RollLog saved = game->log;
    game->log = *edited;

    const GameStatus status = Game_Replay(game);
    if (status == GAME_OK) {
        Game_ReportAfterEdit(game, were_told_complete);
    } else {
        game->log = saved;
        if (Game_Replay(game) != GAME_OK) {
            Fault_Stop("game: the replay that undoes a rejected edit failed; the PinCountRule "
                       "is not pure");
        }
    }
    return status;
}

GameStatus Game_CorrectRoll(Game *game, RollNumber roll_number, Pins pins)
{
    const RollEdit edit = { roll_number, 1U, &pins, 1U };
    return Game_EditRolls(game, &edit);
}

GameStatus Game_EditRolls(Game *game, const RollEdit *edit)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->busy) {
        return GAME_ERR_BUSY;
    }
    RollLog edited;
    GameStatus status = RollLog_Edit(&game->log, edit, &edited);
    if (status == GAME_OK) {
        game->busy = true;
        status = Game_ApplyEditedLog(game, &edited);
        game->busy = false;
    }
    return status;
}
