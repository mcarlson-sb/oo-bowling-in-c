#include "game.h"

#include <stdbool.h>
#include <stddef.h>

#include "fault.h"
#include "frame_reporter.h"
#include "roll_log.h"
#include "scorecard.h"
#include "slot_pool.h"

#define GAME_POOL_SIZE 2U

struct Game {
    Scorecard scorecard;
    PinCountRule count_pins;
    FrameReporter reporter;
    bool busy; /* see Game_Roll */
    RollLog log;
};

static Game s_games[GAME_POOL_SIZE];
static bool s_in_use[GAME_POOL_SIZE];
static SlotPool s_pool = { s_in_use, GAME_POOL_SIZE, s_games, sizeof(Game) };

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
    Game *game = SlotPool_Take(&s_pool);
    if (game == NULL) {
        return NULL;
    }
    Scorecard_Init(&game->scorecard);
    game->count_pins = count_pins;
    FrameReporter_Init(&game->reporter);
    game->busy = false;
    RollLog_Init(&game->log);
    return game;
}

/* A busy destroy stops rather than being ignored: the leaked slot would surface later, far
 * away, as Game_Create returning NULL. */
void Game_Destroy(Game *game)
{
    if (!SlotPool_Holds(&s_pool, game)) {
        return;
    }
    if (game->busy) {
        Fault_Stop("game: destroyed while busy, from inside its own callback or rule");
    }
    if (!SlotPool_Return(&s_pool, game)) {
        Fault_Stop("game: destroyed twice");
    }
}

static GameStatus Game_CheckCanChange(const Game *game)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->busy) {
        return GAME_ERR_BUSY;
    }
    return GAME_OK;
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
    const GameStatus can_change = Game_CheckCanChange(game);
    if (can_change != GAME_OK) {
        return can_change;
    }
    game->busy = true;
    const GameStatus status = Game_Accept(game, pins);
    if (status == GAME_OK) {
        RollLog_Append(&game->log, pins);
        FrameReporter_AfterRoll(&game->reporter, &game->scorecard);
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

bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context)
{
    if (Game_CheckCanChange(game) != GAME_OK) {
        return false;
    }
    if (!FrameReporter_Add(&game->reporter, callback, context)) {
        return false;
    }
    game->busy = true;
    FrameReporter_CatchUpNewest(&game->reporter, &game->scorecard);
    game->busy = false;
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
    const uint8_t were_told_complete = FrameReporter_FramesToldComplete(&game->reporter);
    const RollLog saved = game->log;
    game->log = *edited;

    const GameStatus status = Game_Replay(game);
    if (status == GAME_OK) {
        FrameReporter_AfterEdit(&game->reporter, &game->scorecard, were_told_complete);
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
    GameStatus status = Game_CheckCanChange(game);
    if (status != GAME_OK) {
        return status;
    }
    RollLog edited;
    status = RollLog_Edit(&game->log, edit, &edited);
    if (status == GAME_OK) {
        game->busy = true;
        status = Game_ApplyEditedLog(game, &edited);
        game->busy = false;
    }
    return status;
}
