# Architecture

How the bowling scorer is put together, in pictures. The [README](README.md) explains *why*
the code is written this way; this document shows *what* is where and how the pieces
connect. Every name below is a real type, function or file in `src/` or `include/`.

- [1. The layers](#1-the-layers)
- [2. Public and private headers](#2-public-and-private-headers)
- [3. The classes](#3-the-classes)
- [4. Objects in memory](#4-objects-in-memory)
- [5. Virtual dispatch](#5-virtual-dispatch)
- [6. The frame state machines](#6-the-frame-state-machines)
- [7. Two families of states](#7-two-families-of-states)
- [8. A roll's journey](#8-a-rolls-journey)
- [9. The tests](#9-the-tests)

---

## 1. The layers

Each layer talks only to the one below it. Only the top layer is visible to callers.

```
   caller (any C code)
        |
        |  Game_Create  Game_CreateWithRule  Game_Destroy
        |  Game_OnFrameChanged  Game_Roll  Game_EditRolls  Game_CorrectRoll  Game_Score
        v
+--------------------------------------------------------------+
|  Game                                   include/game.h       |  public
|  opaque handle, pool of 2 games,        src/game.c           |
|  checks each roll and counts it with its PinCountRule        |
+--------------------------------------------------------------+
        |  Scorecard_IsOver / _PinsStanding / _Roll / _Score
        v
+--------------------------------------------------------------+
|  Scorecard  (value)                     src/scorecard.*      |
|  the ten frames; runs a roll along the chain of frames, and  |
|  starts a new frame when none keeps it                       |
+--------------------------------------------------------------+
        |  FrameContext_Roll / _Score / _IsComplete / _PinsStanding
        v
+--------------------------------------------------------------+
|  FrameContext  (State-pattern context)  src/frame_context.*  |
|  holds the current state, and a family of states to switch to|
+--------------------------------------------------------------+
        |  Frame_Roll / Frame_Score / Frame_PinsStanding
        v
+--------------------------------------------------------------+
|  The states                                                  |
|  RegularFrame  StrikeFrame  SpareFrame      src/*_frame.*    |
|  TenthStrikeFrame  TenthSpareFrame          src/tenth_frame.*|
+--------------------------------------------------------------+
        |  Frame_AddRoll  Frame_Complete  Frame_PinsKnockedDown ...
        v
+--------------------------------------------------------------+
|  Frame  (abstract base class)           src/frame.*          |
|  vtable pointer, two RollLists, complete flag                |
+--------------------------------------------------------------+
        |  RollList_Add / _At / _Sum / _Count / _IsFull
        v
+--------------------------------------------------------------+
|  RollList  (value type)                 src/roll_list.*      |
|  up to two pin counts, bounds-checked                        |
+--------------------------------------------------------------+

  Pins, Score (include/bowling_types.h) are used at every layer, and RollNumber and
  FrameNumber wherever a roll or a frame is numbered from 1.
```

**Inside `Game`: three value types**, each held by value. `game.c` itself holds the pool, a
roll's checks and the caller's rule, the busy guard, and replaying the log after an edit:

```
+--------------------------------------------------------------+
|  Game                                   src/game.c           |
+--------------------------------------------------------------+
     |  Scorecard_*           |  FrameReporter_AfterRoll   |  RollLog_Append / _At
     |  (above)               |  / _AfterEdit / _Add       |         RollEdit_Apply
     |                        |  / _CatchUpNewest          |               |
     v                        v                            v               v
+----------------+   +-----------------------+   +----------------+ +--------------+
|  Scorecard     |   |  FrameReporter        |   |  RollLog       | |  RollEdit    |
|  the frames    |   |  what the listeners   |   |  every roll,   | |  which edits |
|                |   |  hear: compares the   |   |  as the pins   | |  a log can   |
|                |   |  Scorecard with what  |   |  that fell     |<|  take, and   |
|                |   |  they were last told  |   |                | |  the log     |
|  src/          |   |  src/frame_reporter.* |   |  src/          | |  each makes  |
|  scorecard.*   |   +-----------------------+   |  roll_log.*    | |  src/        |
+----------------+       |  FrameListeners_Add   +----------------+ |  roll_edit.* |
         ^               |  / _Tell / _TellNewest                   +--------------+
         |               v
         |       +-----------------------+
         +-------|  FrameListeners       |
     reads it    |  who to tell; keeps   |
                 |  and tells them       |
                 |  src/frame_listeners.*|
                 +-----------------------+
```

`RollEdit` holds no state: it reads one `RollLog` through its functions and builds another.

**The re-entry guard is the game's.** `Game` has one `busy` flag, set for the whole of a roll
(`Game_Roll`) and of an edit (`Game_EditRolls`, and so `Game_CorrectRoll`), and while a new
listener is caught up. The caller's code runs inside all three: the rule while a roll is
counted, the listeners while frames are told. While `busy` is set, a roll, an edit or a drain
gets `GAME_ERR_BUSY`, adding a listener gets `false`, and `Game_Destroy` stops
the program. Reading the game (`Game_Score`) is always allowed.

`FrameReporter` is the one bridge between the frames and the listeners: it reads the
`Scorecard` and tells the `FrameListeners`, and it owns `frames_told_complete`, how many frames
they have been told are complete. It never changes the `Scorecard`.

**Beside the game: the pinsetter.** The one part of the system that runs on another thread
(in firmware, an interrupt handler). It never calls the game; the main loop carries each roll
across.

```
   interrupt handler                         main loop
        |                                        |
        |  Pinsetter_Post                        |  Pinsetter_Drain / _DiscardOldest / _RollsLost
        v                                        v
+--------------------------------------------------------------+
|  Pinsetter                              include/pinsetter.h  |  public
|  lock-free ring of 21 rolls, one writer per position         |
|  +--------------------------+  +---------------------------+ |
|  | interrupt side           |  | main-loop side, and the   | |
|  | src/pinsetter_isr.c      |  | pool of 2                 | |
|  | Post, Enqueue            |  | src/pinsetter.c           | |
|  | (nothing else)           |  | Drain, DiscardOldest,     | |
|  |                          |  | RollsLost                 | |
|  +--------------------------+  +---------------------------+ |
|  the ring they share: src/pinsetter_ring.h                   |
+--------------------------------------------------------------+
        |  Game_Roll, on the main loop's thread, oldest first
        v
      Game  (above)
```

---

## 2. Public and private headers

Each header is listed with the headers it includes. Callers can reach only the public part:
CMake gives them `include/` and keeps `src/` for the library itself
(`target_include_directories(bowling PUBLIC include PRIVATE src)`).

```
PUBLIC   include/
  game.h
  '-- bowling_types.h
  pinsetter.h
  |-- bowling_types.h
  '-- game.h
  fault.h                        (no includes; a target may supply Fault_Stop)

PRIVATE  src/
  frame_context.h
  |-- frame.h
  |   |-- bowling_types.h        (public)
  |   '-- roll_list.h
  |       '-- bowling_types.h    (public)
  |-- regular_frame.h  --> frame.h
  |-- strike_frame.h   --> frame.h
  |-- spare_frame.h    --> frame.h
  '-- tenth_frame.h    --> frame.h

  frame_transition.h   --> frame.h

  scorecard.h       --> bowling_types.h (public), frame_context.h
  frame_reporter.h  --> bowling_types.h, game.h (public), frame_listeners.h, scorecard.h
  frame_listeners.h --> bowling_types.h, game.h   (public)
  roll_log.h        --> bowling_types.h (public), game_limits.h
  roll_edit.h       --> game.h (public), roll_log.h

  slot_pool.h                    (standard headers only)
  game_limits.h                  (GAME_MAX_ROLLS; no includes)
  pinsetter_hooks.h --> pinsetter.h  (the overlap check's switch, and a test hook)
  pinsetter_ring.h  --> bowling_types.h, game_limits.h, pinsetter.h, pinsetter_hooks.h
                        (the struct both pinsetter sides share, and the ring's loads and
                        stores, each with its memory order)
  frame_families.h  --> bowling_types.h (public), frame.h
                        (struct FrameStateFactory, and the two families)

Source files that include a header from another module:
  game.c           --> fault.h, frame_reporter.h, roll_edit.h, roll_log.h, scorecard.h,
                       slot_pool.h
  roll_edit.c      --> game_limits.h
  pinsetter.c      --> fault.h, pinsetter_hooks.h, pinsetter_ring.h, slot_pool.h
  pinsetter_isr.c  --> fault.h, pinsetter_hooks.h, pinsetter_ring.h
  pinsetter_hooks.c --> pinsetter_ring.h (the test hook; compiled with the overlap check)
  frame_context.c  --> frame_families.h, frame_transition.h
  frame_families.c --> frame_context.h
  regular_frame.c  --> frame_transition.h (the one state that switches states)
```

Includes only point from `src/` to `include/`, never the other way: no public header
mentions a private one.

`struct Game` is defined only in `game.c`: nothing outside it needs its layout.
`struct FrameStateFactory` is in `frame_families.h`, which only `frame_context.c` and
`frame_families.c` include. `struct Pinsetter` is in
`pinsetter_ring.h`, because its two sides live in two files, so that the interrupt side can have
a stack limit of its own; only those two files include it. The other structs are in
headers because another struct holds them by value (section 4).

---

## 3. The classes

**Inheritance: each state *is a* `Frame`.** Each class's vtable is a `static const` table in
its own `.c` file.

```
                          +--------------------------------+
                          | Frame               (abstract) |
                          |--------------------------------|
                          | vtable      : FrameVtable *    |
                          | rolls       : RollList         |
                          | bonus_rolls : RollList         |
                          | complete    : bool             |
                          |--------------------------------|
                          | roll(context, pins)    virtual |
                          | pins_standing()        virtual |
                          | Frame_Score()                  |
                          | Frame_AddRoll(), ...           |
                          +--------------------------------+
                                          ^
                                          | is a
        +----------------+----------------+----------------+----------------+
        |                |                |                |                |
+-------+------+ +-------+------+ +-------+------+ +-------+------+ +-------+------+
| RegularFrame | | StrikeFrame  | | SpareFrame   | | TenthStrike- | | TenthSpare-  |
|              | |              | |              | | Frame        | | Frame        |
|--------------| |--------------| |--------------| |--------------| |--------------|
| roll:        | | roll:        | | roll:        | | roll:        | | roll:        |
|  strike? or  | |  bonus roll, | |  bonus roll, | |  fill ball,  | |  fill ball,  |
|  spare? then | |  pass it on  | |  pass it on  | |  keep it     | |  keep it     |
|  switch;     | |              | |              | |              | |              |
|  else keep   | |              | |              | |              | |              |
| pins_        | | pins_        | | pins_        | | pins_        | | pins_        |
|  standing:   | |  standing:   | |  standing:   | |  standing:   | |  standing:   |
|  own         | |  default     | |  default     | |  own         | |  default     |
+--------------+ +--------------+ +--------------+ +--------------+ +--------------+
   frames 1-9       frames 1-9       frames 1-9        frame 10         frame 10
   and frame 10
```

"default" means the state's vtable points at the base's `Frame_AllPinsStanding`; "own"
means it points at its own function.

**Composition: a `Frame` *has* two `RollList`s.**

```
+-----------------------+  rolls        +---------------------------+
| Frame                 |<>------------>| RollList        (value)   |
|                       |  bonus_rolls  |---------------------------|
|                       |<>------------>| pins  : Pins[2]           |
+-----------------------+               | count : uint8_t           |
                                        |---------------------------|
                                        | RollList_Add  (bounds-    |
                                        |   checked), _At, _Sum,    |
                                        |   _Count, _IsFull         |
                                        +---------------------------+
```

States never touch the `RollList`s directly; they go through `Frame`'s functions.

---

## 4. Objects in memory

There is no heap. Every object is a fixed-size struct held *by value* inside the next, so
the whole game is laid out at compile time. Sizes are from the compiler on a 64-bit host
(measured with `sizeof` and `offsetof`); a 32-bit MCU, with 4-byte pointers, is smaller.

```
s_games[2]                                          2,096 bytes
+-------------------------------------------------------------+
| Game [0]                                        1,048 bytes |
|  +-------------------------------------------------------+  |
|  | scorecard   : Scorecard                  968 bytes    |  |
|  |  frames[10] : FrameContext               10 x 96      |  |
|  |  +-------------------------------------------------+  |  |
|  |  | FrameContext                        96 bytes    |  |  |
|  |  |  current_state : Frame *  -------------+        |  |  |
|  |  |  factory       : FrameStateFactory *   |        |  |  |
|  |  |  regular       : RegularFrame     16 <-+ in use |  |  |
|  |  |  spare         : SpareFrame       16            |  |  |
|  |  |  strike        : StrikeFrame      16            |  |  |
|  |  |  tenth_spare   : TenthSpareFrame  16            |  |  |
|  |  |  tenth_strike  : TenthStrikeFrame 16            |  |  |
|  |  +-------------------------------------------------+  |  |
|  |  ... 9 more                                           |  |
|  |  frame_count : uint8_t                                |  |
|  | count_pins      : PinCountRule      the caller's rule |  |
|  | reporter        : FrameReporter           48 bytes    |  |
|  |  listeners : FrameListeners               40 bytes    |  |
|  |   entries[2], count             (callback, context)   |  |
|  |  frames_told_complete : uint8_t                       |  |
|  | busy            : bool       a roll or edit under way |  |
|  | log             : RollLog                 22 bytes    |  |
|  |   pins[21], count                                     |  |
|  +-------------------------------------------------------+  |
| Game [1]                                                    |
+-------------------------------------------------------------+

s_pinsetters[2]                                        72 bytes
+-------------------------------------------------------------+
| Pinsetter [0]                                      36 bytes |
|  rolls[22]  : Pins      21 waiting at most; one slot spare  |
|  post_at    : atomic_uint      written by the interrupt side|
|  drain_at   : atomic_uint      written by the main loop     |
|  rolls_lost : _Atomic uint16_t posts refused, ever          |
|  draining   : bool             a drain under way            |
|  posting    : atomic_flag  a post under way (debug builds)  |
| Pinsetter [1]                                               |
+-------------------------------------------------------------+
```

Beside them, `s_in_use[2]` holds one `bool` per game. `game.c` owns both arrays and gives
their addresses to a `SlotPool` (`src/slot_pool.c`), which hands out and takes back the games
themselves (`SlotPool_Take`, `SlotPool_Return`), and tells whether a pointer is one of them
(`SlotPool_Holds`) by comparing addresses, never reading it. `pinsetter.c` does the same for
the pinsetters.

Each context owns storage for every state it could be in, and `current_state` points at
the one in use. Changing state rebuilds that slot in place (`FrameContext_NewStrikeFrame`
and the others) and repoints `current_state`; nothing is allocated.

**Why "base struct first" makes inheritance work.** A derived struct starts with its
`Frame`, so both have the same address, and a pointer to one is a valid pointer to the
other:

```
StrikeFrame *strike  ----+
                         |
Frame *frame  -----------+-> +--------------------------------------------+
                             | base : Frame                               |   both point here:
                             |   vtable -----> s_vtable (StrikeFrame's)   |   the same address
                             |   rolls        { pins: [10, -], count: 1 } |
                             |   bonus_rolls  { pins: [-, -], count: 0 }  |
                             |   complete     false                       |
                             +--------------------------------------------+
                             | (a derived class would add fields here)    |
                             +--------------------------------------------+
```

So `Frame *frame = &strike->base;` gives the same address as `strike` itself: code holding
either pointer sees the same object.

---

## 5. Virtual dispatch

A call on a `Frame` goes through its vtable to whichever state it really is.
`Frame_Roll` also handles complete frames itself first (Template Method), so no state has to.

```
FrameContext_Roll(context, pins)
        |
        v
Frame_Roll(current_state, context, pins)          src/frame.c
        |
        |-- complete? --yes--> return Passed(pins)     (no state is called)
        |
        no
        v
current_state->vtable->roll(current_state, context, pins)
        |
        +---> RegularFrame_Roll      if current_state is a RegularFrame
        +---> StrikeFrame_Roll       if it is a StrikeFrame
        +---> SpareFrame_Roll        ...
        +---> TenthStrikeFrame_Roll
        '---> TenthSpareFrame_Roll
```

The vtable is chosen once, when the state is built, in its `_Init` function:

```
Frame *StrikeFrame_Init(StrikeFrame *self)             static const FrameVtable s_vtable = {
{                                                          .roll          = StrikeFrame_Roll,
    return Frame_InitStrike(&self->base, &s_vtable);  -->  .pins_standing = Frame_AllPinsStanding,
}                                                      };
```

---

## 6. The frame state machines

The full transition table, and the reasons behind the design, are in
[STATE_PATTERN.md](STATE_PATTERN.md).

Every frame starts as a `RegularFrame`. Only `RegularFrame` changes state: it calls
`FrameContext_SetState` on the context passed into its `roll()`.

**Frames 1 to 9.** Strike and spare frames pass their bonus rolls on, because the same rolls
also start the next frame.

```
 ( start )
     |
     v
 [ RegularFrame ] -- first roll = 10 --> [ StrikeFrame ] -- 2nd bonus roll --------+
     |       |                             takes 2 bonus rolls, passes each on     |
     |       |                                                                     |
     |       '-- rolls total 10 -------> [ SpareFrame ] -- bonus roll -------------+
     |                                     takes 1 bonus roll, passes it on        |
     |                                                                             v
     '-- two rolls, total < 10 ---------------------------------------------------( complete )
```

**Frame 10.** Same start, but a strike or spare keeps its fill balls: there is no next frame.

```
 ( start )
     |
     v
 [ RegularFrame ] -- first roll = 10 --> [ TenthStrikeFrame ] -- 2nd fill ball ----+
     |       |                             takes 2 fill balls, keeps them          |
     |       |                                                                     |
     |       '-- rolls total 10 -------> [ TenthSpareFrame ] -- fill ball ---------+
     |                                     takes 1 fill ball, keeps it             |
     |                                                                             v
     '-- two rolls, total < 10 ---------------------------------------------------( complete )
```

When the tenth frame is complete, the game is over. A frame scores 0 until it is complete,
then the sum of its rolls and bonus rolls (`Frame_Score`, the same for every state).

---

## 7. Two families of states

`RegularFrame` never knows which frame it is in. It asks its context for "the strike state"
or "the spare state", and the context builds one from the **family** it was given when the
frame started (Abstract Factory).

```
Scorecard_AddNewFrame
   |
   |-- frames 1 to 9 --> FrameContext_Init -------> factory = &FrameFamily_Passing
   |                                                new_strike --> StrikeFrame
   |                                                new_spare  --> SpareFrame
   |
   '-- frame 10 -------> FrameContext_InitTenth --> factory = &FrameFamily_Tenth
                                                    new_strike --> TenthStrikeFrame
                                                    new_spare  --> TenthSpareFrame

Later, on a strike, the same code runs in every frame:

RegularFrame_Roll
   '--> FrameContext_NewStrikeFrame(context)
           '--> context->factory->new_strike(context)
                   '--> whichever strike state this frame's family builds
```

Both tables are `const`, in `src/frame_families.c`; `frame_context.h` only forward-declares
`struct FrameStateFactory`.

---

## 8. A roll's journey

**`Game_Roll`, step by step.** All five checks run before any frame sees the roll, so a
rejected roll changes nothing.

```
Game_Roll(game, pins)
   |
   |-- game == NULL?                      --yes--> GAME_ERR_NULL_GAME
   |-- game busy (inside a listener or   --yes--> GAME_ERR_BUSY
   |   the rule)?
   |
   |   busy = true, until the roll returns
   |  Game_Accept:
   |-- tenth frame complete?              --yes--> GAME_ERR_GAME_OVER
   |      (Scorecard_IsOver)
   |-- pins > pins standing?              --yes--> GAME_ERR_INVALID_PINS
   |      (Scorecard_PinsStanding asks the latest frame: Frame_PinsStanding)
   |
   |   counted = game->count_pins(pins standing, pins)
   |      (the game's PinCountRule: standard, or one the caller supplied)
   |
   |-- counted > pins standing?           --yes--> GAME_ERR_RULE_OUT_OF_RANGE
   v
Scorecard_Roll
   |  Scorecard_ApplyPinsToFrames: offer the counted roll to each frame, oldest first
   |
   |   frame 1 --> frame 2 --> ... --> latest frame
   |     each one either keeps the roll (consumed: stop)
   |     or passes it on (a complete frame, or a strike/spare taking a bonus)
   v
nobody kept it?  --yes--> Scorecard_AddNewFrame: start a new frame with this roll
   |
   v
FrameReporter_AfterRoll, from the first frame not yet reported: tell each listener
   |   (FrameListeners_Tell) about every frame this roll completed, oldest first
   v
GAME_OK
```

This is Chain of Responsibility, with a twist: a strike or spare frame both *acts on* a
roll (records it as a bonus) and *passes it on*.

**The rule is applied once, at the door.** `Game_Accept` counts a roll with the game's
`PinCountRule` before any frame sees it, so every frame, including an earlier strike taking
it as a bonus, sees the same counted value. The frames never know a rule exists.

**Telling listeners, after the roll.** Once a roll has gone all the way through, the game's
`FrameReporter` tells each listener (up to two, set with `Game_OnFrameChanged`) about every
frame the roll completed, oldest first. A frame never completes before the one before it, so
the reporter only keeps a count of frames already reported, checks the frames just past it,
and asserts, in a debug build, that every frame before the count is complete. Frames don't
hold listeners, and no listener is called while a roll is half applied. A listener may read
the game (`Game_Score` then sees the whole roll), but not change it: the game is busy, so a
roll, an edit or a drain from inside a listener, or from inside the rule, is refused. A
listener added mid-game is caught up at once: it, and only it, is told every frame already
complete, with the game busy as for any callback.

**Editing rolls.** The game keeps every accepted roll in a log, as the pins that fell. One
operation, `Game_EditRolls`, replaces a range of rolls with new ones: replacing,
inserting and deleting are all the same edit. `Game_CorrectRoll` is one roll out and one in.

```
Game_EditRolls(game, &edit)     edit: a RollEdit {first_roll, rolls_removed, new_pins, new_count}
   |
   |-- game == NULL?                        --yes--> GAME_ERR_NULL_GAME
   |-- game busy (inside a listener or the rule)?  --yes--> GAME_ERR_BUSY
   |
   |  RollEdit_Apply(edit, &game->log, &edited):
   |-- range not rolls the log has?         --yes--> GAME_ERR_NO_SUCH_ROLL
   |   (starts after the last roll, or new rolls promised, but NULL)
   |-- more than 21 rolls after the edit?   --yes--> GAME_ERR_TOO_MANY_ROLLS
   |   edited = the rolls before the range, the new rolls, the rolls after it
   v
Game_ApplyEditedLog
   |   saved = game->log             (a copy of plain values: safe, unlike the frames)
   |   game->log = edited
   |   Game_Replay: Scorecard_Init, then Game_Accept every roll, in order
   |      (each one counted again by the game's PinCountRule)
   |
   |-- a roll is now impossible?   --yes--> game->log = saved, replay that,
   |                                        and return the impossible roll's status
   v
FrameReporter_AfterEdit, from the first frame: tell each frame again, the final state only
   |   a complete frame   --> (number, new score, complete = true)
   |   a reopened frame   --> (number, 0,         complete = false)
   v
GAME_OK
```

An edit:
- **is checked, and told, only in its final state,** however many rolls it replaces;
- **is undone by copying the log back** (22 bytes of plain values), never the game: each
  `FrameContext` points at one of its own state slots, so a copy would point into the original;
- **replays the pins that fell,** which the log keeps, so the rule counts every roll again.

**Rolls from the pinsetter.** The interrupt handler and the main loop share one ring, and
each writes only its own position, so neither needs a lock:

```
interrupt handler: Pinsetter_Post(pinsetter, pins)
   |
   |-- (debug) another post under way?  --yes--> Fault_Stop: only one handler may post
   |
   |   next = the slot after post_at (wrapping after the 22nd)
   |-- next == drain_at (acquire)?  --yes--> full: count it in rolls_lost, return false
   |
   |   rolls[post_at] = pins
   |   post_at = next (release)          the main loop may now see the roll
   v
true

main loop: Pinsetter_Drain(pinsetter, game)
   |
   |   end = post_at (acquire), read once: rolls posted from now on wait for the next drain
   |-- drain_at == end?  --yes--> GAME_OK: nothing (more) was waiting
   |
   |   status = Game_Roll(game, rolls[drain_at])
   |-- the game rejected it?  --yes--> return its status; the roll stays waiting
   |      (from inside a listener, that is every roll: GAME_ERR_BUSY)
   |
   |   drain_at = the next slot (release)   the interrupt side may reuse this one
   '-- and round again
```

Each side publishes its position with a release store after touching the roll, and reads the
other's with an acquire load before touching one, so a roll's bytes are always visible before
the position that announces them.

A drain that stops leaves the roll waiting. The scorer then corrects an earlier roll
(`Game_CorrectRoll`), or throws the waiting one away (`Pinsetter_DiscardOldest`, refused while
a drain is running); the next drain carries on. A drain takes only the rolls waiting when it
starts, at most 21, and rolls reported after the tenth frame wait the same way, for the next
game. The ring holds a whole game's rolls, a static assert tying its size to
`GAME_MAX_ROLLS`. The lost-roll count is never cleared, so each reader takes its own
difference.

**Worked example: rolls 10, 3, 4.** `RollResult` is what each frame hands back.

```
roll | frame 1                         | frame 2                        | score
-----+---------------------------------+--------------------------------+------
 10  | (no frames yet: new frame)      |                                |
     | RegularFrame: strike!           |                                |
     |  -> becomes StrikeFrame         |                                |
     |  -> consumed                    |                                |   0
-----+---------------------------------+--------------------------------+------
  3  | StrikeFrame: bonus 1 = 3        | (nobody kept it: new frame)    |
     |  -> passed on (3)               | RegularFrame: first roll 3     |
     |                                 |  -> consumed                   |   0
-----+---------------------------------+--------------------------------+------
  4  | StrikeFrame: bonus 2 = 4        |                                |
     |  -> complete: 10 + 3 + 4 = 17   |                                |
     |  -> passed on (4)               | RegularFrame: second roll 4    |
     |                                 |  -> complete: 3 + 4 = 7        |
     |                                 |  -> consumed                   |  24
```

The 3 and the 4 each count twice, once as frame 1's bonus and once as frame 2's own roll,
which is exactly the bowling rule. Before the 4, frame 2's `pins_standing` is 7, so a 4 is
accepted and an 8 would be rejected.

---

## 9. The tests

```
black box: include/ only, like a caller          white box: one private type, directly
  game_test.cpp               scoring, the pool,   roll_list_test.cpp          RollList
                              the tenth frame,     roll_log_test.cpp           RollLog
                              input, NULL          slot_pool_test.cpp          SlotPool
  nine_pin_no_tap_test.cpp    a caller's rule      frame_listeners_test.cpp    FrameListeners
  scoreboard_test.cpp         listeners            frame_test.cpp              Frame's roll()
  correction_test.cpp         edits                                            contract
  pinsetter_test.cpp          the pinsetter        pinsetter_overlap_test.cpp  the one-producer
  remote_scoreboard_test.cpp  a listener over a                                check, through
                              wire                                             pinsetter_hooks.h
     sharing test/test_support.h: GameHandle, RollAll, the client-side rules
```

- **Death tests** cover every fail-stop (`Fault_Stop`, in every build) and every debug
  `assert` on a caller's bug. Where a debug build stops and a release build refuses, as for
  the bounds of `RollList` and `RollLog`, each build has its own test.
- **Property tests** in `correction_test.cpp` judge random corrections and edits, under the
  standard rule and under no-tap, against a fresh game fed the edited rolls.
- **One test uses a real second thread**, as the pinsetter's interrupt side, through five games;
  CI runs it under ThreadSanitizer. The other pinsetter tests play the interrupt side
  themselves, between main-loop calls or from inside a listener.
- **Every test runs in four builds**: debug, release (`NDEBUG`), UBSan and ThreadSanitizer (in
  CI, on Linux), with warnings as errors. `GameHandle`, a `std::unique_ptr` with `Game_Destroy`
  as its deleter, keeps any test from leaking a game into the next.
