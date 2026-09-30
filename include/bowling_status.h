#ifndef BOWLING_STATUS_H
#define BOWLING_STATUS_H

/* What a roll or an edit can come back with. Every error status leaves the game unchanged. */

typedef enum {
    GAME_OK = 0,
    GAME_ERR_GAME_OVER,
    GAME_ERR_INVALID_PINS,
    GAME_ERR_NO_SUCH_ROLL,      /* an edit's range isn't rolls the game has had */
    GAME_ERR_TOO_MANY_ROLLS,    /* an edit would leave more balls than the variant's longest game */
    GAME_ERR_TOO_MANY_SUBSCRIBERS,
    GAME_ERR_NOT_SUBSCRIBED,
    GAME_ERR_INVALID_RULES,     /* rules the scorer can't play */
    GAME_ERR_NO_GAME,           /* a request before any NEW_GAME has started a game */
    GAME_ERR_GAME_IN_PROGRESS,  /* a NEW_GAME while a game is still in play */
    GAME_ERR_NOT_IN_PRACTICE,   /* an END_PRACTICE with no practice going on */
    GAME_ERR_CERTIFIED,         /* a change to a game once it is certified */
    GAME_ERR_NOT_OVER           /* a CERTIFY before the scorer says the game is over */
} GameStatus;

#endif /* BOWLING_STATUS_H */
