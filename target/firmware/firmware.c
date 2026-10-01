#include "firmware.h"

void Firmware_HostTheLanes(void)
{
    ActorHost_Start(FIRMWARE_GAME_PRIORITY, FIRMWARE_OBSERVER_PRIORITY);
    ActorHost_HostGame(FIRMWARE_LANE_1_GAME_ID, FIRMWARE_GAME_PRIORITY);
    ActorHost_HostScoreboard(FIRMWARE_LANE_0_SCOREBOARD_ID);
    ActorHost_HostScoreboard(FIRMWARE_LANE_1_SCOREBOARD_ID);
}
