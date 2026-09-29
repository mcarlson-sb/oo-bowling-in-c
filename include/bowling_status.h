#ifndef BOWLING_STATUS_H
#define BOWLING_STATUS_H

/* What a roll or an edit can come back with. Shared by the Game facade and the scorer core.
 * Every error status leaves the game unchanged. */

typedef enum {
    GAME_OK = 0,
    GAME_ERR_GAME_OVER,         /* the tenth frame is complete */
    GAME_ERR_INVALID_PINS,      /* more pins than are standing */
    GAME_ERR_NULL_GAME,
    GAME_ERR_RULE_OUT_OF_RANGE, /* the PinCountRule counted more pins than were standing */
    /* Called from inside a frame-changed callback or the PinCountRule, while a roll or an
     * edit is in progress; or Pinsetter_Drain called from inside a drain. */
    GAME_ERR_BUSY,
    GAME_ERR_NO_SUCH_ROLL,      /* an edit's range isn't rolls the game has had */
    GAME_ERR_TOO_MANY_ROLLS,    /* an edit would leave more than 21 rolls */
    GAME_ERR_INVALID_EDIT,      /* a NULL edit, or one that promises rolls but gives no pins */
    GAME_ERR_NO_ROOM,           /* the game actor has no room for another subscriber */
    GAME_ERR_NOT_SUBSCRIBED     /* an unsubscribe from one that isn't subscribed */
} GameStatus;

#endif /* BOWLING_STATUS_H */
