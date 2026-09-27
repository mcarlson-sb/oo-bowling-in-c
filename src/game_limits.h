#ifndef GAME_LIMITS_H
#define GAME_LIMITS_H

/* The most rolls a game can take: two in each of frames 1 to 9, and three in the tenth. The
 * game's roll log holds that many, and so does the pinsetter's mailbox, so that a drain
 * stopped at a roll the game refused can't cost the machine a roll of the game. */
#define GAME_MAX_ROLLS 21U

#endif /* GAME_LIMITS_H */
