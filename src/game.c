#include "game.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "frame_context.h"
#include "game_limits.h"
#include "slot_pool.h"

/* Frames in a game of bowling. The last one keeps its own fill balls, so a game never needs
 * more. */
#define GAME_FRAMES 10U

/* Every accepted roll, as the pins that fell. Plain values only, so a RollLog can be copied
 * safely: saving one before an edit, and putting it back, is how a rejected edit is undone.
 * (A Game or FrameContext can't be copied like that: each context points at its own state.) */
typedef struct {
    Pins pins[GAME_MAX_ROLLS];
    uint8_t count;
} RollLog;

/* Games available at once. There is no heap, so games come from a fixed pool. */
#define GAME_POOL_SIZE 2U

/* Who to tell when a frame completes. */
#define GAME_MAX_LISTENERS 2U

typedef struct {
    FrameChangedCallback callback;
    void *context;
} FrameChangedListener;

struct Game {
    FrameContext frames[GAME_FRAMES];
    uint8_t frame_count;
    PinCountRule count_pins; /* how this game counts a roll: see Game_CreateWithRule */
    uint8_t frames_reported; /* frames already told to the listeners */
    FrameChangedListener listeners[GAME_MAX_LISTENERS];
    uint8_t listener_count;
    bool notifying; /* true while the listeners are being told: see Game_Roll */
    RollLog log; /* see Game_CorrectRoll */
    RollLog mailbox; /* rolls made from inside a callback, oldest first: see Game_Roll */
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

static void Game_TellListeners(const Game *game, uint8_t frame_number, Score frame_score,
                               bool frame_complete)
{
    for (uint8_t i = 0U; i < game->listener_count; i++) {
        const FrameChangedListener *listener = &game->listeners[i];
        listener->callback(listener->context, frame_number, frame_score, frame_complete);
    }
}

/* Tells the listeners about every frame that has completed since the last roll.
 *
 * A frame never completes before the one before it: its bonus rolls, if any, are the next
 * frames' own rolls. So the frames already reported are always the first few, and one roll
 * can only complete frames just past them, in order. Keeping a count of frames reported is
 * enough; no before-and-after copy of every frame's flag is needed. */
static void Game_ReportCompletedFrames(Game *game)
{
    game->notifying = true;
    while ((game->frames_reported < game->frame_count) &&
           FrameContext_IsComplete(&game->frames[game->frames_reported])) {
        const FrameContext *frame = &game->frames[game->frames_reported];
        game->frames_reported++;
        Game_TellListeners(game, game->frames_reported, FrameContext_Score(frame), true);
    }
    game->notifying = false;
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
    game->listener_count = 0U;
    game->notifying = false;
    game->log.count = 0U;
    game->mailbox.count = 0U;
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

/* Accepts one roll, logs it and tells the listeners what it completed. */
static GameStatus Game_RollNow(Game *game, Pins pins)
{
    const GameStatus status = Game_Accept(game, pins);
    if (status == GAME_OK) {
        game->log.pins[game->log.count] = pins;
        game->log.count++;
        Game_ReportCompletedFrames(game);
    }
    return status;
}

/* Takes the oldest roll out of the mailbox. */
static Pins Game_TakeFromMailbox(Game *game)
{
    const Pins oldest = game->mailbox.pins[0];
    game->mailbox.count--;
    for (uint8_t i = 0U; i < game->mailbox.count; i++) {
        game->mailbox.pins[i] = game->mailbox.pins[i + 1U];
    }
    return oldest;
}

/* A roll from inside a callback can't be applied at once: it would complete frames, and
 * tell listeners about them, in the middle of telling them about earlier ones. So it waits
 * in the mailbox, and the roll that started the notification applies it once the listeners
 * have heard everything before it. A queued roll can queue more; they are applied in the
 * order they were made. */
GameStatus Game_Roll(Game *game, Pins pins)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->notifying) {
        if ((game->log.count + game->mailbox.count) >= GAME_MAX_ROLLS) {
            return GAME_ERR_TOO_MANY_ROLLS; /* they can't all be real, and the mailbox is full */
        }
        game->mailbox.pins[game->mailbox.count] = pins;
        game->mailbox.count++;
        return GAME_QUEUED;
    }
    const GameStatus status = Game_RollNow(game, pins);
    while (game->mailbox.count > 0U) {
        (void)Game_RollNow(game, Game_TakeFromMailbox(game));
    }
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
    if ((game == NULL) || (callback == NULL) || (game->listener_count == GAME_MAX_LISTENERS)) {
        return false;
    }
    FrameChangedListener *listener = &game->listeners[game->listener_count];
    listener->callback = callback;
    listener->context = context;
    game->listener_count++;
    return true;
}

/* Empties the frames and replays the roll log from the start. Stops at, and returns the
 * status of, the first roll that can't happen. */
static GameStatus Game_Replay(Game *game)
{
    game->frame_count = 0U;
    for (uint8_t i = 0U; i < game->log.count; i++) {
        const GameStatus status = Game_Accept(game, game->log.pins[i]);
        if (status != GAME_OK) {
            return status;
        }
    }
    return GAME_OK;
}

/* After a correction: tells the listeners about each frame again. A complete frame is sent
 * with its new score. A frame they were told was complete, but no longer is, is sent with
 * complete = false. `was_reported` is how many frames they had been told were complete.
 * Complete frames always come first, so the new count of frames reported is simply how
 * many are complete. */
static void Game_ReportCorrection(Game *game, uint8_t was_reported)
{
    game->notifying = true;
    game->frames_reported = 0U;
    const uint8_t frames = (was_reported > game->frame_count) ? was_reported : game->frame_count;
    for (uint8_t i = 0U; i < frames; i++) {
        const uint8_t frame_number = (uint8_t)(i + 1U);
        if ((i < game->frame_count) && FrameContext_IsComplete(&game->frames[i])) {
            Game_TellListeners(game, frame_number, FrameContext_Score(&game->frames[i]), true);
            game->frames_reported = frame_number;
        } else if (i < was_reported) {
            Game_TellListeners(game, frame_number, 0U, false); /* reopened */
        }
    }
    game->notifying = false;
}

/* Makes `edited` the game's roll log and rescores by replaying it. If some roll in it is
 * impossible, the saved log is put back and replayed. Every roll in that one was accepted
 * before, so it can't fail as long as the rule is pure (see PinCountRule). Returns the
 * status of the first impossible roll, or GAME_OK. */
static GameStatus Game_ApplyEditedLog(Game *game, const RollLog *edited)
{
    const uint8_t was_reported = game->frames_reported;
    const RollLog saved = game->log; /* plain values: safe to copy, unlike the frames */
    game->log = *edited;

    const GameStatus status = Game_Replay(game);
    if (status == GAME_OK) {
        Game_ReportCorrection(game, was_reported);
    } else {
        game->log = saved;
        const GameStatus restored = Game_Replay(game);
        assert(restored == GAME_OK);
        (void)restored; /* used only by the assert, which NDEBUG removes */
    }
    return status;
}

GameStatus Game_CorrectRoll(Game *game, uint8_t roll_number, Pins pins)
{
    return Game_EditRolls(game, roll_number, 1U, &pins, 1U); /* one roll out, one in */
}

GameStatus Game_EditRolls(Game *game, uint8_t first_roll, uint8_t rolls_removed,
                          const Pins *new_pins, uint8_t new_count)
{
    if (game == NULL) {
        return GAME_ERR_NULL_GAME;
    }
    if (game->notifying) {
        return GAME_ERR_EDIT_DURING_NOTIFICATION;
    }
    const uint8_t first = (uint8_t)(first_roll - 1U); /* index of the first roll replaced */
    if ((first_roll == 0U) || (first_roll > game->log.count) ||
        ((first + rolls_removed) > game->log.count)) {
        return GAME_ERR_NO_SUCH_ROLL; /* an edit starts at a roll the game has had */
    }
    if ((new_pins == NULL) && (new_count > 0U)) {
        return GAME_ERR_NO_SUCH_ROLL; /* new rolls promised, but none given */
    }
    const unsigned new_length = ((unsigned)game->log.count - rolls_removed) + new_count;
    if (new_length > GAME_MAX_ROLLS) {
        return GAME_ERR_TOO_MANY_ROLLS; /* no game has that many, and the log has no room */
    }

    /* The rolls before the range, then the new rolls, then the rolls after it. */
    RollLog edited;
    edited.count = 0U;
    for (uint8_t i = 0U; i < first; i++) {
        edited.pins[edited.count++] = game->log.pins[i];
    }
    for (uint8_t i = 0U; i < new_count; i++) {
        edited.pins[edited.count++] = new_pins[i];
    }
    for (uint8_t i = (uint8_t)(first + rolls_removed); i < game->log.count; i++) {
        edited.pins[edited.count++] = game->log.pins[i];
    }
    return Game_ApplyEditedLog(game, &edited);
}
