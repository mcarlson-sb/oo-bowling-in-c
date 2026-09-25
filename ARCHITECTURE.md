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
        |  Game_Create  Game_Roll  Game_Score  Game_Destroy
        v
+--------------------------------------------------------------+
|  Game                                   include/game.h       |  public
|  opaque handle, pool of 2 games,        src/game.c           |
|  runs each roll along the chain of frames                    |
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

  Pins, Score (include/bowling_types.h) are used at every layer.
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

Source files that include a header from another module:
  game.c           --> frame_context.h
  regular_frame.c  --> frame_context.h    (the one state that switches states)
```

Includes only point from `src/` to `include/`, never the other way: no public header
mentions a private one.

`struct Game` is defined only in `game.c`, and `struct FrameStateFactory` only in
`frame_context.c`: nothing outside those files needs their layout. The other structs are in
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
(checked with `_Static_assert`); a 32-bit MCU, with 4-byte pointers, is smaller.

```
s_pool[2]                                           1,936 bytes
+-------------------------------------------------------------+
| Game [0]                                        968 bytes   |
|  +-------------------------------------------------------+  |
|  | frames[10]  : FrameContext                 10 x 96    |  |
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
|  | frame_count : uint8_t                                 |  |
|  | in_use      : bool                                    |  |
|  +-------------------------------------------------------+  |
| Game [1]                                                    |
+-------------------------------------------------------------+
```

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
Frame *StrikeFrame_Init(StrikeFrame *self)        static const FrameVtable s_vtable = {
{                                                     .roll          = StrikeFrame_Roll,
    Frame_Init(&self->base, &s_vtable);  ------>      .pins_standing = Frame_AllPinsStanding,
    ...                                           };
}
```

---

## 6. The frame state machines

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
Game_AddNewFrame
   |
   |-- frames 1 to 9 --> FrameContext_Init -------> factory = &s_regular_family
   |                                                new_strike --> StrikeFrame
   |                                                new_spare  --> SpareFrame
   |
   '-- frame 10 -------> FrameContext_InitTenth --> factory = &s_last_frame_family
                                                    new_strike --> TenthStrikeFrame
                                                    new_spare  --> TenthSpareFrame

Later, on a strike, the same code runs in every frame:

RegularFrame_Roll
   '--> FrameContext_NewStrikeFrame(context)
           '--> context->factory->new_strike(context)
                   '--> whichever strike state this frame's family builds
```

Both tables are `static const` in `src/frame_context.c`; the header only forward-declares
`struct FrameStateFactory`.

---

## 8. A roll's journey

**`Game_Roll`, step by step.** All three checks run before any frame sees the roll, so a
rejected roll changes nothing.

```
Game_Roll(game, pins)
   |
   |-- game == NULL?                      --yes--> GAME_ERR_NULL_GAME
   |-- tenth frame complete?              --yes--> GAME_ERR_GAME_OVER
   |-- pins > pins standing?              --yes--> GAME_ERR_INVALID_PINS
   |      (asks the latest frame: Frame_PinsStanding)
   v
Game_ApplyPinsToFrames: offer the roll to each frame, oldest first
   |
   |   frame 1 --> frame 2 --> ... --> latest frame
   |     each one either keeps the roll (consumed: stop)
   |     or passes it on (a complete frame, or a strike/spare taking a bonus)
   v
nobody kept it?  --yes--> Game_AddNewFrame: start a new frame with this roll
   |
   v
GAME_OK
```

This is Chain of Responsibility, with a twist: a strike or spare frame both *acts on* a
roll (records it as a bonus) and *passes it on*.

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
test/game_test.cpp  (black box)            test/roll_list_test.cpp  (white box)
  includes: game.h only                      includes: roll_list.h, from src/
  +-----------------------------+            +------------------------------+
  | scoring                     |            | empty, add, sum, full        |
  | end of game                 |            | bounds:                      |
  | game storage (the pool)     |            |   debug:   stops (assert)    |
  | tenth frame                 |            |   release: refused, reads 0  |
  | input validation            |            +------------------------------+
  | NULL handles                |
  +-----------------------------+
            |                                           |
            v                                           v
   public API only, like a real caller         one private type, directly
```

Every test runs in three builds, debug, release (`NDEBUG`) and UBSan, with warnings as
errors. `GameHandle` (a `std::unique_ptr` with `Game_Destroy` as its deleter) makes sure no
test leaks a game from the pool into the next one.
