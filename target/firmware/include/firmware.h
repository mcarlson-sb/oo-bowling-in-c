#ifndef FIRMWARE_H
#define FIRMWARE_H

/* The firmware's actors, the same in every image: the actor host with a game on each of two
 * lanes, each fed by its own pinsetter, and a scoreboard for each lane, hosted by the observers'
 * task. Each lane's game waits for a NEW_GAME, and each scoreboard for a subscription to it, from
 * whoever runs the league. */

#include "actor_host.h"

#define FIRMWARE_LANES 2U
/* Lane 0's game is at ACTOR_HOST_GAME_ID; the rest follow. */
#define FIRMWARE_LANE_1_GAME_ID 2U
#define FIRMWARE_LANE_0_SCOREBOARD_ID 3U
#define FIRMWARE_LANE_1_SCOREBOARD_ID 4U

/* Every game's task below the observers', which take each event as a game sends it. */
#define FIRMWARE_GAME_PRIORITY 2U
#define FIRMWARE_OBSERVER_PRIORITY 3U

/* Before the scheduler starts: the host, both lanes and their scoreboards. */
void Firmware_HostTheLanes(void);

#endif /* FIRMWARE_H */
