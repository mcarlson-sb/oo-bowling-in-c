#include "game.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "fault.h"
#include "frame_context.h"
#include "frame_listeners.h"
#include "roll_log.h"
#include "slot_pool.h"

/* Frames in a game of bowling. The last one keeps its own fill balls, so a game never needs
 * more. */
#define GAME_FRAMES 10U

/* Games available at once. There is no heap, so games come from a fixed pool. */
#define GAME_POOL_SIZE 2U

struct Game {
    FrameContext frames[GAME_FRAMES];
    uint8_t frame_count;
    PinCountRule count_pins; /* how this game counts a roll: see Game_CreateWithRule */
    uint8_t frames_reported; /* frames already told to the listeners */
    FrameListeners listeners; /* who to tell when a frame changes: see Game_OnFrameChanged */
    bool busy; /* true for the whole of a roll or an edit: see Game_Roll */
    RollLog log; /* see Game_CorrectRoll */
};

/* The games themselves, and the pool that tracks which are in use. The pool's bookkeeping
 * lives in slot_pool.c; this file only owns the storage. */
static Game s_games[GAME_POOL_SIZE];
static bool s_in_use[GAME_POOL_SIZE];
static SlotPool s_pool = { s_in_use, GAME_POOL_SIZE };

static bool Game_IsOver(const Game *game)
{
    return (game->frame_count == GAME_FRAMES) &&
           FrameContext_IsComplete(&game->frames[GAME_FRAMES - 1U]);
}

static bool Game_HasNoFrames(const Game *game)
{
    return game->frame_count == 0U;
}

/* Only the latest frame can still be taking its own rolls, so only it can leave fewer than
 * ten pins standing. */
static Pins Game_PinsStanding(const Game *game)
{
    if (Game_HasNoFrames(game)) {
        return FRAME_ALL_PINS;
    }
    return FrameContext_PinsStanding(&game->frames[game->frame_count - 1U]);
}

static void Game_AddNewFrame(Game *game, Pins pins)
{
    FrameContext *new_frame = &game->frames[game->frame_count];
    if (game->frame_count == (GAME_FRAMES - 1U)) {
        FrameContext_InitTenth(new_frame);
    } else {
        FrameContext_Init(new_frame);
    }
    const RollResult result = FrameContext_Roll(new_frame, pins);
    assert(result.consumed); /* a new frame always keeps its first roll */
    (void)result;            /* used only by the assert, which NDEBUG removes */
    game->frame_count++;
}

/* Offers the roll to each frame, oldest first, until one keeps it (Chain of Responsibility).
 * A frame that passes it on hands it to the next; a frame that keeps it ends the chain. */
static RollResult Game_ApplyPinsToFrames(Game *game, Pins pins)
{
    RollResult result = RollResult_Passed(pins);
    for (uint8_t i = 0U; (i < game->frame_count) && !result.consumed; i++) {
        result = FrameContext_Roll(&game->frames[i], result.pins);
    }
    return result;
}

/* Tells the listeners about the frames from index `first` on, as they are now. A complete
 * frame is sent with its score. A frame they were told was complete, but no longer is, is sent
 * with complete = false. `was_reported` is how many frames they had been told were complete.
 * Complete frames always come first (a frame's bonus rolls are the next frames' own rolls), so
 * the count reported is simply how many are complete.
 *
 * Inline, like the two below: every listener callback runs on top of these, so their frames
 * are paid under each one. As an ordinary call it added 112 bytes there (release, 64-bit
 * host). Callers use the two below, which say which walk they want. */
static inline void Game_ReportFrames(Game *game, uint8_t first, uint8_t was_reported)
{
    game->frames_reported = first;
    const uint8_t frames = (was_reported > game->frame_count) ? was_reported : game->frame_count;
    for (uint8_t i = first; i < frames; i++) {
        const FrameNumber frame_number = (FrameNumber)(i + 1U); /* frames count from 1 */
        if ((i < game->frame_count) && FrameContext_IsComplete(&game->frames[i])) {
            FrameListeners_Tell(&game->listeners, frame_number,
                                FrameContext_Score(&game->frames[i]), true);
            game->frames_reported = frame_number;
        } else if (i < was_reported) {
            FrameListeners_Tell(&game->listeners, frame_number, 0U, false); /* reopened */
        }
    }
}

/* After a roll: nothing before the frames already reported can have changed, and nothing can
 * have reopened, so the walk starts at the first frame not yet reported. */
static inline void Game_ReportAfterRoll(Game *game)
{
    Game_ReportFrames(game, game->frames_reported, game->frames_reported);
}

/* After an edit: any frame can have changed, or reopened, so the walk starts at the first
 * frame. `was_reported` is how many the listeners had been told were complete before it. */
static inline void Game_ReportAfterEdit(Game *game, uint8_t was_reported)
{
    Game_ReportFrames(game, 0U, was_reported);
}

/* Standard bowling: a roll counts as the pins it knocked down. */
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
    game->frame_count = 0U;
    game->count_pins = count_pins;
    game->frames_reported = 0U;
    FrameListeners_Init(&game->listeners);
    game->busy = false;
    RollLog_Init(&game->log);
    return game;
}

/* Finds which slot a game is in. Compares addresses for equality only, so a pointer that
 * isn't one of ours (including NULL) is simply not found. */
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

void Game_Destroy(Game *game)
{
    uint8_t slot = 0U;
    if (Game_FindSlot(game, &slot)) {
        SlotPool_Release(&s_pool, slot);
    }
}

/* Checks a roll and, if it can happen, applies it to the frames. Tells no one: a caller that
 * wants listeners told does that itself. */
static GameStatus Game_Accept(Game *game, Pins pins)
{
    /* Every check runs before any frame sees the roll. Frames act on a roll as it passes
     * through them, and that can't be undone, so this is what leaves a rejected roll with
     * no effect. */
    if (Game_IsOver(game)) {
        return GAME_ERR_GAME_OVER;
    }
    const Pins pins_standing = Game_PinsStanding(game);
    if (pins > pins_standing) {
        return GAME_ERR_INVALID_PINS;
    }

    const Pins pins_counted = game->count_pins(pins_standing, pins);
    if (pins_counted > pins_standing) {
        return GAME_ERR_RULE_OUT_OF_RANGE; /* the rule is caller code: check it, don't trust it */
    }
    const RollResult result = Game_ApplyPinsToFrames(game, pins_counted);
    if (!result.consumed) {
        Game_AddNewFrame(game, result.pins);
    }
    return GAME_OK;
}

/* The game is busy for the whole of a roll, and of an edit: the caller's code runs in the
 * middle of both, the rule while a roll is counted and the listeners while the frames are told.
 * A roll or an edit from there would change the game under the one in progress, and tell the
 * listeners about frames out of order, so it is refused. */
GameStatus Game_Roll(Game *game, Pins pins)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->busy) {
        return GAME_ERR_DURING_NOTIFICATION;
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

    Score score = 0U;
    for (uint8_t i = 0U; i < game->frame_count; i++) {
        score = (Score)(score + FrameContext_Score(&game->frames[i]));
    }
    return score;
}

bool Game_OnFrameChanged(Game *game, FrameChangedCallback callback, void *context)
{
    if (game == NULL) {
        return false;
    }
    return FrameListeners_Add(&game->listeners, callback, context);
}

/* Empties the frames and replays the roll log from the start. Stops at, and returns the
 * status of, the first roll that can't happen. */
static GameStatus Game_Replay(Game *game)
{
    game->frame_count = 0U;
    for (uint8_t i = 0U; i < RollLog_Count(&game->log); i++) {
        const GameStatus status = Game_Accept(game, RollLog_At(&game->log, i));
        if (status != GAME_OK) {
            return status;
        }
    }
    return GAME_OK;
}

/* Makes `edited` the game's roll log and rescores by replaying it. If some roll in it is
 * impossible, the saved log is put back and replayed. Every roll in that one was accepted
 * before, so it can't fail as long as the rule is pure (see PinCountRule). If it does fail,
 * the rule has broken that contract, and no game is left that is known to be right: there is
 * no safe state to fall back to, so the program stops, in every build. Returns the status of
 * the first impossible roll, or GAME_OK. */
static GameStatus Game_ApplyEditedLog(Game *game, const RollLog *edited)
{
    const uint8_t was_reported = game->frames_reported;
    const RollLog saved = game->log; /* plain values: safe to copy, unlike the frames */
    game->log = *edited;

    const GameStatus status = Game_Replay(game);
    if (status == GAME_OK) {
        Game_ReportAfterEdit(game, was_reported);
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
    const RollEdit edit = { roll_number, 1U, &pins, 1U }; /* one roll out, one in */
    return Game_EditRolls(game, &edit);
}

GameStatus Game_EditRolls(Game *game, const RollEdit *edit)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->busy) {
        return GAME_ERR_DURING_NOTIFICATION;
    }
    RollLog edited;
    GameStatus status = RollLog_Edit(&game->log, edit, &edited);
    if (status == GAME_OK) {
        game->busy = true; /* see Game_Roll */
        status = Game_ApplyEditedLog(game, &edited);
        game->busy = false;
    }
    return status;
}
