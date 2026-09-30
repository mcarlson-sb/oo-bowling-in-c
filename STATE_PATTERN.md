# State, without the State Pattern

On `kay-oo`, a frame was an object in the State pattern:
- each kind of frame (open, spare, strike, tenth) was a state, with a vtable of function
  pointers;
- a frame changed class as its balls came in;
- an abstract factory chose each state's family.

That design is intact on `kay-oo` and `main`, and this document's history describes it in depth.

On `rtos-actor` there are no function pointers (JPL's Power of Ten rule 9), so no vtables and no
State pattern. The code still has states, and still changes behavior with them. This document
is about where each state machine lives now, how it is written, and what that gains and loses.

## 1. Three state machines, three shapes

| State machine | Its states | Written as | Why that shape |
|---|---|---|---|
| The lane, in the scorer | Taking frames, taking fill balls, over | An enum, `LanePhase`, and one `switch` in `Lane_Throw` | Three states, all derived from the balls. The walk recomputes the phase every time, so nothing can go stale |
| The game's lifecycle | Awaiting rules, in play | An enum, `GameLifecycle`, that `GameActor_Handle` switches on first | Forced by NEW_GAME: before it, the scorer has no rules, and every request must be answered "no game" |
| What a kind makes of a message | Each selector it responds to, and "doesn't understand" | A table per kind, from the protocol's selector to that kind's own request enum, then a `switch` on the request | The table is the kind's protocol, Smalltalk's `respondsTo:`, as data. An unlisted selector reads as "doesn't understand" by construction |

## 2. The lane: an enum and a switch

A frame no longer has a class. The scorer keeps only the balls, and the lane works everything
out by walking them through the rules:

```c
static void Lane_Throw(Lane *lane, const ScorerRules *rules, uint8_t ball_index, Pins pins)
{
    switch (lane->phase) {
    case LANE_TAKING_FRAMES:
        Lane_ThrowInFrame(lane, rules, ball_index, pins);
        break;
    case LANE_TAKING_FILL_BALLS:
        Lane_ThrowFillBall(lane, rules, pins);
        break;
    case LANE_OVER:
    default:
        assert(false && "Lane_CheckBall refuses a ball once the lane is over");
        break;
    }
}
```

What kay-oo's frame states did is now data:
- **A strike and a spare** are a frame that clears the rack with its first or second ball.
- **A candlepin ten-box** clears it with the third.
- **The bonus each is owed** comes from the rules' `bonus_balls_by_clearing_ball`.
- **The tenth frame's fill balls** are the lane's second phase.

So a new variant is a new set of rules, sent in a message, and not a new class: a 5-frame game,
or a 3-ball game at a rack of 5, plays with no code change.

**What it gains:** every transition is in one function, visible to the call graph, and the
compiler's `-Wswitch` checks every phase is handled.

**What it loses:** the State pattern's open-closed property. A new phase means editing this
switch. Here the phases are fixed by the game, and the variation moved into the rules instead.

## 3. The lifecycle: dispatch on state, then on selector

```c
void GameActor_Handle(GameActor *self, const Message *message, Outbox *outbox)
{
    outbox->count = 0U;
    if (message->envelope.selector == MSG_NEW_GAME) {
        GameActor_NewGame(self, message, outbox);
        return;
    }
    switch (self->lifecycle) {
    case GAME_AWAITING_RULES:
        GameActor_BeforeAGame(self, message, outbox);
        break;
    case GAME_IN_PLAY:
        GameActor_Receive(self, message, outbox);
        break;
    }
}
```

This is Smalltalk's `become:`, as an enum. The actor's own state decides what a message does now.
- **Before a game,** the pinsetter's rolls are held for the first game, and anything else is
  answered "no game".
- **In play,** the request is looked up and handled.

**NEW_GAME is decided outside the lifecycle switch,** and that is deliberate:
- It is the lifecycle's own message, the one that moves it, so it is taken before the state is
  consulted.
- It needs to know whether a game is still in play, and that is the scorer's to say, not a
  third lifecycle state. An edit after the last ball can reopen a finished game, and a copy of
  "over" in the lifecycle could go stale.
- Taken first, it keeps the request switch at 9 cases, under ENG-3.1's complexity limit of 10.
  As a tenth case, the switch would have crossed it.

## 4. A kind's protocol: a table, then a switch

```c
static const ScoreboardRequest k_scoreboard_protocol[MSG_SELECTOR_COUNT] = {
    [MSG_FRAME_CHANGED] = SCOREBOARD_FRAME_CHANGED,
    [MSG_QUERY_FIGURE] = SCOREBOARD_QUERY_FIGURE,
    [MSG_REPLY] = SCOREBOARD_REPLY,
};
```

Every other selector reads as 0, `SCOREBOARD_DOES_NOT_UNDERSTAND`. So a selector the protocol
gains later is not understood by every kind until someone lists it. The switch on the kind's own
request enum has no `default`, so `-Wswitch` still checks every request a kind lists is
handled.

**The alternative, a switch on the selector with a `default:`,** would be as clear for the
scoreboard's three selectors. But the `default` would hide a selector added later from the
compiler's check. For the game, whose request switch sits at the complexity limit, the table
keeps it under the limit as well. Whether that earns its keep, or only moves a branch out of
the count, is in the phase 3 report.

## 5. The one switch on the kind

Which receive function hears a message is decided once, in the shell:

```c
switch (route->kind) {
case ACTOR_KIND_GAME:
    GameActor_Handle(&self->games[route->instance], message, outbox);
    break;
case ACTOR_KIND_SCOREBOARD:
    Scoreboard_Handle(&self->scoreboards[route->instance], message, outbox);
    break;
case ACTOR_KIND_RUNNING_AVERAGE:
    RunningAverage_Handle(&self->running_averages[route->instance], message, outbox);
    break;
case ACTOR_KIND_EXTERNAL:
case ACTOR_KIND_NONE:
    GameShell_CountDropped(self);
    break;
}
```

That is the whole of the polymorphism: what a vtable did, as a switch on data, with every target
a direct call. Two costs are recorded in RTOS_ACTOR.md:
- **Duplicate Switch Case.** Each kind's own switch on the selector is the smell the
  constitution calls a Duplicate Switch Case, and its remedy, a function-pointer table, is the
  one thing this code can't have.
- **Stack.** Every hosted task runs through this switch, so every task's stack is sized for
  the deepest kind.

## 6. Testing the state machines

- **The lane:** random games against three independent references, and the refusals: a ball
  after the game, more pins than stand, and rules the scorer can't play.
- **The lifecycle:**
  - "no game" before one;
  - a NEW_GAME refused mid-game;
  - a new game after one ends;
  - the held rolls played into the new game;
  - the subscribers told the old frames reopened.
- **The protocols:**
  - every kind answers NOT_UNDERSTOOD to a selector it doesn't list, and to one past the
    protocol's end;
  - and never answers a NOT_UNDERSTOOD, so two kinds can't echo one forever.
