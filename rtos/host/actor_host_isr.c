#include "actor_host.h"
#include "actor_host_lanes.h"

void ActorHost_PinsetterCountedFromIsr(ActorHostLane lane, Pins pins)
{
    Pinsetter_CountedFromIsr(ActorHost_PinsetterOfLane(lane), pins);
}
