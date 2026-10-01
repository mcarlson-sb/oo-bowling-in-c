#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

/* What each message means to a game in each of its states: private to the game actor. The game
 * works out its state from what it stores; this says what a selector means there. */

#include <stdbool.h>

#include "bowling_status.h"
#include "message.h"

/* The state a message is read in: the lifecycle's decision, and the facts, whether rolls are held
 * and whether the scorer says the game is over. */
typedef enum {
    GAME_STATE_AWAITING_RULES,
    GAME_STATE_PRACTICE,
    GAME_STATE_IN_PLAY,
    GAME_STATE_HOLDING, /* in play, with rolls held */
    GAME_STATE_OVER,    /* in play, nothing held, and the scorer says it's over */
    GAME_STATE_CERTIFIED,
    GAME_STATES
} GameState;

/* What a message means to the game in each state: what it asks the game to do, and whether it
 * moves the game's lifecycle. Every entry names both, one of them nothing. */
typedef enum {
    GAME_DOES_NOT_UNDERSTAND = 0, /* what a missing entry reads as */
    GAME_NOTHING_ASKED,           /* a move, which the lifecycle's own switch makes */
    GAME_REFUSE,                  /* with the entry's reason */
    GAME_ANSWER_FIGURE,
    GAME_ANSWER_STATS,
    GAME_SUBSCRIBE,
    GAME_UNSUBSCRIBE,
    GAME_ROLL,
    GAME_PLAY_OR_HOLD,
    GAME_HOLD_THE_ROLL,
    GAME_EDIT,
    GAME_DISCARD_HELD,
    GAME_HEAR_LOST_REPORT,
    GAME_COUNT_PRACTICE_BALL,
    GAME_REFUSE_THE_PINSETTERS_ROLL
} GameRequest;

typedef enum {
    GAME_NO_MOVE = 0,
    GAME_NEW_GAME,
    GAME_END_PRACTICE,
    GAME_PINSETTER_DOWN,
    GAME_PINSETTER_UP,
    GAME_CERTIFY
} GameMove;

typedef struct {
    GameRequest request;
    GameMove move;
    GameStatus refused_for; /* GAME_REFUSE's reason; GAME_OK otherwise */
} GameMeaning;

/* What `selector` means to a game in `state`, with its pinsetter down or not. */
GameMeaning GameProtocol_MeaningOf(GameState state, bool pinsetter_down, Selector selector);

#endif /* GAME_PROTOCOL_H */
