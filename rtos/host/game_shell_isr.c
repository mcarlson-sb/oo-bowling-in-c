#include "game_shell.h"
#include "game_shell_lanes.h"

void GameShell_PinsetterCountedFromIsr(GameShellLane lane, Pins pins)
{
    Pinsetter_CountedFromIsr(GameShell_PinsetterOfLane(lane), pins);
}
