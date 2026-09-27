# The State Pattern, in Depth

This is the design pattern at the heart of the scorer. This document covers what problem it
solves, how each part of it is built in C, the full transition table, the decisions behind
it, the alternatives that were rejected, and how to add a state.

For the overall structure see [ARCHITECTURE.md](ARCHITECTURE.md); for why the code is
object-oriented at all see the [README](README.md).

- [1. The problem](#1-the-problem)
- [2. The pattern](#2-the-pattern)
- [3. The roles in this code](#3-the-roles-in-this-code)
- [4. Building it in C](#4-building-it-in-c)
- [5. The complete transition table](#5-the-complete-transition-table)
- [6. A transition, step by step](#6-a-transition-step-by-step)
- [7. Design decisions](#7-design-decisions)
- [8. Alternatives, and why they weren't used](#8-alternatives-and-why-they-werent-used)
- [9. Adding a state](#9-adding-a-state)
- [10. Testing the state machine](#10-testing-the-state-machine)
- [11. Checking it as an explicit state machine](#11-checking-it-as-an-explicit-state-machine)
- [12. What it costs](#12-what-it-costs)

---

## 1. The problem

A bowling frame's behavior depends on what has already happened in it:

| What has happened | What the next roll means | Does the frame keep the roll? | Score so far |
|---|---|---|---|
| Nothing | The frame's first roll | Yes | 0 (incomplete) |
| One roll, under 10 | The frame's second roll | Yes | 0 (incomplete) |
| A strike | A bonus roll, which also starts the next frame | No, it passes it on | 0 until 2 bonus rolls |
| A spare | A bonus roll, which also starts the next frame | No, it passes it on | 0 until 1 bonus roll |
| A strike, in the tenth frame | A fill ball, and there is no next frame | Yes | 0 until 2 fill balls |
| Complete | Nothing: the roll belongs to a later frame | No, it passes it on | Final |

There are three operations whose answers depend on that history: *take this roll*, *how many
pins are standing?*, and *what is the score?* Written the obvious way, each one becomes a
`switch` on a frame-type flag, and the same `switch` is repeated in every function that
cares. Section 8 shows exactly what that looks like.

## 2. The pattern

The State pattern, from *Design Patterns* by Gamma, Helm, Johnson and Vlissides (the "Gang
of Four", GoF), has this intent:

> Allow an object to alter its behavior when its internal state changes. The object will
> appear to change its class.

Instead of one object with a flag and a `switch` in every method, the behavior for each
state lives in its own object. The object the rest of the program talks to, the
**context**, holds a reference to the current state and forwards calls to it. Changing state
means pointing the context at a different state object.

```
            +-------------+   current_state   +-----------------+
 caller --> |   Context   |------------------>|  State          |  (interface)
            |-------------|                   |-----------------|
            | request()   |                   | handle()        |
            |  forwards   |                   +-----------------+
            |  to state   |                      ^     ^     ^
            +-------------+                      |     |     |
                                        +--------+  +--+--+  +--------+
                                        | StateA |  |StateB|  | StateC |
                                        +--------+  +-----+  +--------+
```

## 3. The roles in this code

| GoF role | Here | Where |
|---|---|---|
| **Context** | `FrameContext`, one per frame | `src/frame_context.h/.c` |
| **State (interface)** | `FrameVtable`, the operations every state provides | `src/frame.h` |
| **State (shared base)** | `Frame`: the fields and behavior all states share | `src/frame.h/.c` |
| **Concrete states** | `RegularFrame`, `StrikeFrame`, `SpareFrame`, `TenthStrikeFrame`, `TenthSpareFrame` | `src/*_frame.c`, `src/tenth_frame.c` |
| **Requests** | `FrameContext_Roll`, `FrameContext_PinsStanding`, `FrameContext_Score`, `FrameContext_IsComplete` | `src/frame_context.c` |
| **Client** | `Game`, which holds ten contexts and never knows what state any of them is in | `src/game.c` |

The states and what they mean:

| State | Used in | Meaning |
|---|---|---|
| `RegularFrame` | every frame, at its start | Taking the frame's own rolls; not a strike or spare (yet) |
| `StrikeFrame` | frames 1 to 9 | A strike, collecting 2 bonus rolls, passing each one on |
| `SpareFrame` | frames 1 to 9 | A spare, collecting 1 bonus roll, passing it on |
| `TenthStrikeFrame` | frame 10 | A strike, collecting 2 fill balls, keeping them |
| `TenthSpareFrame` | frame 10 | A spare, collecting 1 fill ball, keeping it |

*Complete* is not a state of its own. It's the `complete` flag on the base, which every state
can reach, and which the base handles in one place (section 4.5).

## 4. Building it in C

C has no classes or interfaces, so each part of the pattern is written out by hand.

### 4.1 The state interface: a vtable

The operations every state must provide are a struct of function pointers:

```c
/* src/frame.h */
/* The state interface: every state keeps this contract, so any can stand in for a Frame. */
typedef struct {
    /* Given an incomplete frame, no more pins than pins_standing() allows, and the frame's own
     * context (passed in, so no state points back to it). Records the roll as a roll or bonus
     * roll, and returns Consumed, or Passed(pins) for the next frame too. Changes state only
     * through `context`. */
    RollResult (*roll)(Frame *self, struct FrameContext *context, Pins pins);

    /* For the next roll, if this is the latest frame. No side effects. */
    Pins (*pins_standing)(const Frame *self);
} FrameVtable;
```

Only behavior that really differs between states is in the vtable. Scoring is the same for
every state, so `Frame_Score` is an ordinary function on the base (section 4.6).

### 4.2 A concrete state

Each state is a struct whose **first member** is the base `Frame`, plus a `.c` file holding
its functions and a `static const` vtable pointing at them. Here is `StrikeFrame` in full:

```c
/* src/strike_frame.h */
typedef struct {
    Frame base; /* must be first: StrikeFrame "extends" Frame */
} StrikeFrame;

Frame *StrikeFrame_Init(StrikeFrame *self);
```

```c
/* src/strike_frame.c */
static RollResult StrikeFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    (void)context;
    Frame_AddBonusRoll(self, pins);
    if (Frame_HasAllBonusRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Passed(pins);
}

static const FrameVtable s_vtable = {
    .roll = StrikeFrame_Roll,
    .pins_standing = Frame_AllPinsStanding,
};

Frame *StrikeFrame_Init(StrikeFrame *self)
{
    return Frame_InitStrike(&self->base, &s_vtable);
}
```

What a strike *is*, one roll of all the pins, is the base's to say, once, in
`Frame_InitStrike`: both strike states, `StrikeFrame` and `TenthStrikeFrame`, build themselves
with it and differ only in the vtable they pass. The spares do the same with
`Frame_InitSpare`.

Three C techniques are doing the work here:
- **`static` functions** can't be called from any other file. The only way to reach
  `StrikeFrame_Roll` is through the vtable.
- **The `static const` vtable** is built at compile time and can live in flash. Every
  `StrikeFrame` shares the one table.
- **`_Init` returns `Frame *`**, the base type, which is what the context stores. That's valid
  because `base` is the first member, so the two share an address.

### 4.3 The context

`FrameContext` holds a pointer to the current state, and **the storage for every state it can
be in**:

```c
/* src/frame_context.h */
typedef struct FrameContext {
    Frame *current_state;
    const struct FrameStateFactory *factory;
    RegularFrame regular;
    SpareFrame spare;
    StrikeFrame strike;
    TenthSpareFrame tenth_spare;
    TenthStrikeFrame tenth_strike;
} FrameContext;
```

Every request is a one-line forward to the current state:

```c
/* src/frame_context.c */
RollResult FrameContext_Roll(FrameContext *self, Pins pins)
{
    return Frame_Roll(self->current_state, self, pins);
}

Pins FrameContext_PinsStanding(const FrameContext *self)
{
    return Frame_PinsStanding(self->current_state);
}
```

`Game` calls these and never asks what state a frame is in. There is no `switch` on a state
anywhere in `game.c`.

**A context can't be copied.** `current_state` points at one of the context's *own* slots,
such as `&self->regular`. Copy the struct, and the copy's `current_state` still points into
the original. Once the original is gone or changes, the copy is running a state that isn't
its own. So a frame is an object, with an identity, not a value. Anything that needs a copy,
such as undoing a rejected edit, copies plain values instead (the roll log) and rebuilds the
frames from them. `KAY.md` shows the probe that found this: after a copy-back, every frame
pointed outside its game
([phase 5b, section 2](KAY.md#2-replay-into-a-scratch-copy-would-have-left-dangling-pointers)).

### 4.4 Transitions: the states decide

In this design **the state decides when to change state**. Only `RegularFrame` ever does:

```c
/* src/regular_frame.c */
static RollResult RegularFrame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    if (RegularFrame_IsStrike(self, pins)) {
        FrameContext_SetState(context, FrameContext_NewStrikeFrame(context));
        return RollResult_Consumed();
    }
    if (RegularFrame_IsSpare(self, pins)) {
        FrameContext_SetState(context, FrameContext_NewSpareFrame(context, self, pins));
        return RollResult_Consumed();
    }

    Frame_AddRoll(self, pins);
    if (Frame_HasAllRolls(self)) {
        Frame_Complete(self);
    }
    return RollResult_Consumed();
}
```

The guards are small named predicates, each a pure function of the frame and the roll:

```c
static bool RegularFrame_IsStrike(const Frame *self, Pins pins)
{
    return Frame_NextIsFirstRoll(self) && (pins == FRAME_ALL_PINS);
}

static bool RegularFrame_IsSpare(const Frame *self, Pins pins)
{
    return (Frame_PinsKnockedDown(self) + pins) == FRAME_ALL_PINS;
}
```

`IsStrike` is checked first on purpose. On a first roll of 10, `IsSpare` would also be true
(0 + 10 == 10), and the frame would wrongly become a spare. The history shows a test that
passed for exactly that wrong reason before `StrikeFrame` existed.

**How a state reaches the context.** The context passes itself as an argument to `roll()`.
This is one of the options the GoF describe, and it means a state doesn't have to store a
pointer back to its context. The states that don't change state ignore it with
`(void)context;`.

**What a state can see of the context.** Only the three functions it needs:
`FrameContext_SetState`, `FrameContext_NewStrikeFrame` and `FrameContext_NewSpareFrame`, in
`src/frame_transition.h`. `Game` needs a different set (roll, score, is it complete, pins
standing), in `src/frame_context.h`. Each client sees only its own (Interface Segregation):
`RegularFrame` never sees the context's layout, and `Game` can't switch a frame's state.

### 4.5 Completed frames: handled once, in the base

A complete frame passes every roll on, whatever state it's in. That rule is written once, in
`Frame_Roll`, before it dispatches:

```c
/* src/frame.c */
RollResult Frame_Roll(Frame *self, struct FrameContext *context, Pins pins)
{
    /* Once here, so no state's roll() ever sees a complete frame. */
    if (self->complete) {
        return RollResult_Passed(pins);
    }
    return self->vtable->roll(self, context, pins);
}
```

This is the **Template Method** pattern: the base fixes the outline of the operation, and the
states fill in only the step that differs. Each state's `roll()` can therefore assume its
frame is incomplete. Every state used to begin with the same guard until it was moved here.

### 4.6 Shared and default behavior

- **`Frame_Score` is not virtual.** Every state scores the same way: 0 until complete, then
  its rolls plus its bonus rolls. A tenth-frame fill ball is stored as a bonus roll, so even
  the tenth frame needs no special case.
- **`pins_standing` has a default.** `Frame_AllPinsStanding` returns 10. `StrikeFrame`,
  `SpareFrame` and `TenthSpareFrame` put it in their vtables as it is, because their next
  roll is always on a full rack. `RegularFrame` and `TenthStrikeFrame` override it with
  their own function.

### 4.7 Creating states without a heap

A textbook State pattern creates each new state with `new` and discards the old one. Here
there is no heap, so the context owns one slot per state (section 4.3). A "new" state is
built in its slot:

```c
Frame *FrameContext_NewStrikeFrame(FrameContext *self)
{
    return self->factory->new_strike(self);
}
```

**Which** strike state that is depends on the frame: a strike in frame 3 passes its bonus rolls
on, but a strike in frame 10 keeps its fill balls. So each context is started with a
**family** of states, a `const` factory table (the Abstract Factory pattern):

```c
/* src/frame_context.c */
static const struct FrameStateFactory s_regular_family = {
    .new_strike = NewPassingStrike,   /* builds a StrikeFrame */
    .new_spare = NewPassingSpare,     /* builds a SpareFrame */
};

static const struct FrameStateFactory s_last_frame_family = {
    .new_strike = NewTenthStrike,     /* builds a TenthStrikeFrame */
    .new_spare = NewTenthSpare,       /* builds a TenthSpareFrame */
};
```

`FrameContext_Init` gives frames 1 to 9 the regular family, and `FrameContext_InitTenth`
gives frame 10 the last-frame family. Both start in `RegularFrame`. So `RegularFrame` is the
same code in every frame, and never needs to know which frame it's in.

## 5. The complete transition table

The event is **a roll of `p` pins**. By the time a state sees it, `Game_Roll` has already
rejected invalid rolls (a game that is over, more pins than are standing), and counted the
roll with the game's `PinCountRule`. So `p` is the *counted* value: under a nine-pin no-tap
rule, a first-ball 9 arrives as a 10, and the states never know a rule exists.

| Current state | Condition | Next state | Recorded as | Returns |
|---|---|---|---|---|
| any | frame is complete | *(unchanged)* | nothing | passed on (`p`) |
| `RegularFrame` | first roll, `p` = 10 | family's strike state | roll 10 | consumed |
| `RegularFrame` | rolls so far + `p` = 10 | family's spare state | both rolls | consumed |
| `RegularFrame` | first roll, `p` < 10 | `RegularFrame` | roll | consumed |
| `RegularFrame` | second roll, total < 10 | `RegularFrame`, complete | roll | consumed |
| `StrikeFrame` | first bonus roll | `StrikeFrame` | bonus | passed on (`p`) |
| `StrikeFrame` | second bonus roll | `StrikeFrame`, complete | bonus | passed on (`p`) |
| `SpareFrame` | its bonus roll | `SpareFrame`, complete | bonus | passed on (`p`) |
| `TenthStrikeFrame` | first fill ball | `TenthStrikeFrame` | bonus | consumed |
| `TenthStrikeFrame` | second fill ball | `TenthStrikeFrame`, complete | bonus | consumed |
| `TenthSpareFrame` | its fill ball | `TenthSpareFrame`, complete | bonus | consumed |

"Passed on" means the next frame gets the same roll. That's how one roll can be a strike's
bonus and the next frame's first roll at the same time. If no frame keeps a roll, `Game`
starts a new frame with it.

**Pins standing**, which `Game_Roll` checks against before any state sees the roll:

| State | Pins standing for the next roll |
|---|---|
| `RegularFrame`, one roll in | 10 minus that roll |
| `RegularFrame`, otherwise | 10 |
| `StrikeFrame`, `SpareFrame`, `TenthSpareFrame` | 10 (the default: the next roll is on a full rack) |
| `TenthStrikeFrame`, one fill ball in, and it wasn't a strike | 10 minus that fill ball |
| `TenthStrikeFrame`, otherwise | 10 |

The same tables, as diagrams:

```
 frames 1 to 9                                    frame 10

 [ RegularFrame ]                                 [ RegularFrame ]
    |    |    '-- 2 rolls < 10 --> complete          |    |    '-- 2 rolls < 10 --> complete
    |    |                                           |    |
    |    '-- spare --> [ SpareFrame ]                |    '-- spare --> [ TenthSpareFrame ]
    |                   1 bonus, pass on             |                   1 fill ball, keep
    |                   --> complete                 |                   --> complete
    |                                                |
    '-- strike --> [ StrikeFrame ]                   '-- strike --> [ TenthStrikeFrame ]
                    2 bonus, pass on                                 2 fill balls, keep
                    --> complete                                     --> complete
```

**What can't happen, and what stops it:**

| Situation | Handled by |
|---|---|
| A roll after the tenth frame is complete | `Game_Roll` returns `GAME_ERR_GAME_OVER` before any state sees it |
| More pins than are standing | `Game_Roll` returns `GAME_ERR_INVALID_PINS` before any state sees it |
| A caller's rule counting a roll as more pins than were standing | `Game_Roll` returns `GAME_ERR_RULE_OUT_OF_RANGE` before any state sees it |
| A roll reaching a complete frame | `Frame_Roll` passes it on (section 4.5) |
| A state recording more rolls than a frame holds | `RollList_Add` refuses it, and stops a debug build at an assert |
| A spare on the first roll | Can't happen: a first roll of 10 is a strike, and `IsStrike` is checked first |

## 6. A transition, step by step

Frame 3 rolls a 7 and then a 3: a spare. Here is the context before the second roll, while
it runs, and after.

**Before**: the 7 is in; the frame is a `RegularFrame`.

```
FrameContext (frame 3)
  current_state ---> regular  { rolls: [7], bonus: [], complete: false }
                     spare    (unused)
                     strike   (unused)
```

**During** `RegularFrame_Roll(regular, context, 3)`:

1. `IsStrike`: not the first roll, so no.
2. `IsSpare`: 7 + 3 = 10, so yes.
3. `FrameContext_NewSpareFrame(context, self, 3)` asks the family for its spare state, which
   is `SpareFrame_Init(&context->spare, regular, 3)`. That copies the rolls from `regular`
   (the 7) into the `spare` slot, and adds the 3.
4. `FrameContext_SetState(context, spare)` repoints `current_state`.
5. Returns *consumed*, so the 3 goes no further.

**After**: the frame is a `SpareFrame` waiting for its bonus roll.

```
FrameContext (frame 3)
  current_state --+  regular  { rolls: [7], ... }         left behind, never read again
                  '> spare    { rolls: [7, 3], bonus: [], complete: false }
                     strike   (unused)
```

**Why the slots are separate.** In step 3, `SpareFrame_Init` reads the old state (`regular`)
while it writes the new one (`spare`). With a separate slot per state, those are different
memory, so this is safe. If the states shared one slot, for example through a `union`, then
building the spare would overwrite the rolls it was in the middle of copying (section 7).

**Next roll**, a 5: `Game` offers it to frame 3 first. `SpareFrame_Roll` records it as the
bonus, marks the frame complete (7 + 3 + 5 = 15), and passes the 5 on, so it also becomes
frame 4's first roll.

## 7. Design decisions

| Decision | Chosen | Why |
|---|---|---|
| Who changes state | The states | The rules for leaving `RegularFrame` belong to `RegularFrame`. A context that decided would need to know every state's rules, which puts the `switch` back |
| How a state reaches the context | Passed into `roll()` | No stored back-pointer in every state object (5 states × 10 frames). This is the GoF option of the context passing itself |
| Where state objects live | One slot per state, owned by the context | No heap. Each slot is sized at compile time, and old and new states never overlap in memory (section 6) |
| Separate slots, not a `union` | Separate | A union would save 64 bytes per frame, but building a state from the one it replaces would then overwrite its own source. MISRA C also advises against unions |
| Which strike and spare a frame gets | A factory family per context | `RegularFrame` stays identical in every frame. The tenth frame's rules are two states and a table, with no `if (frame == 10)` anywhere |
| Behavior every state shares | On the base, not virtual | `Frame_Score`, the complete-frame rule and the roll bookkeeping are written once. The vtable carries only what really differs |
| "Complete" | A flag on the base, not a state | Every state can become complete, and complete frames all behave the same, so the rule lives in one place (`Frame_Roll`) |

## 8. Alternatives, and why they weren't used

### A flag and a `switch`

The most common way to write this in C:

```c
/* NOT this code: what the State pattern replaces */
typedef enum { FRAME_REGULAR, FRAME_STRIKE, FRAME_SPARE, FRAME_TENTH_STRIKE,
               FRAME_TENTH_SPARE } FrameKind;

RollResult Frame_Roll(Frame *f, Pins pins)
{
    switch (f->kind) {
    case FRAME_REGULAR:      /* strike? spare? record... */       break;
    case FRAME_STRIKE:       /* bonus, pass on... */              break;
    case FRAME_SPARE:        /* bonus, complete, pass on... */    break;
    case FRAME_TENTH_STRIKE: /* fill ball, keep... */             break;
    case FRAME_TENTH_SPARE:  /* fill ball, complete, keep... */   break;
    }
}

Pins Frame_PinsStanding(const Frame *f)
{
    switch (f->kind) {       /* the same five cases again */
    ...
    }
}
```

It starts out simpler: one file, no vtables. But every operation that depends on the state
repeats the same `switch`, so a new state means finding and editing every one of them. The
code-smell catalog calls this *Duplicate Switch Case*. The tenth frame shows the difference:
in this design it was added as two new states and one factory table, and `RegularFrame`,
`StrikeFrame` and `SpareFrame` didn't change.

### A table-driven state machine

Common in firmware: an array of `{ state, event, guard, action, next_state }` rows, run by
one generic engine. It's the best choice when transitions are *data*, especially when there
are many states and events, and when the table itself is what gets reviewed. Here there is
only one event, a roll, and what varies between states is *behavior*: what "record this roll"
means and what to pass on. A table would end up as function pointers for actions and guards,
which is the vtable again, just less directly organized.

### A function pointer as the state

The lightest C idiom: `current_state` is just a pointer to a roll-handling function, and
changing state means assigning a different function. That works for a single operation. This
design has two operations that vary together (`roll` and `pins_standing`), plus per-state
data (rolls, bonus rolls). Keeping them apart would mean two pointers that must be changed in
step, which is what the vtable guarantees.

### The context decides the transitions

The context could look at each roll's result and choose the next state itself. Then the
states wouldn't need the context at all, and the dependency between `regular_frame.c` and the
context would go. But the context would need to know every state's rules, so the
`switch` comes back in a new place. The GoF discuss both options; this code chooses
states-decide.

## 9. Adding a state

The design is open to new states without changing the existing ones. As an illustration,
suppose a house rule made a first-roll 9 a "near miss", worth a one-roll bonus. This is
**hypothetical**, not in the code. The steps:

1. **Write the tests first,** in `test/game_test.cpp`, through the public API. For example:
   rolls 9, 1, 4 score whatever the rule says.
2. **Declare the state** in its own header: a struct with `Frame base;` as its first member,
   and an `_Init` that returns `Frame *`.
3. **Implement it** in its `.c` file: a `static` `roll`, a `pins_standing` (or the default
   `Frame_AllPinsStanding`), a `static const` vtable, and `_Init`.
4. **Give it a slot** in `FrameContext`.
5. **Give it a factory entry** in `struct FrameStateFactory`, and fill that entry in both
   families: the passing version for frames 1 to 9 and the keeping version for frame 10, if
   they differ.
6. **Trigger the transition** from the state that moves to it (here, a new guard in
   `RegularFrame_Roll`).
7. **Check the invariants:** a state's rolls and bonus rolls fit in two `RollList`s, and
   `pins_standing` is right for every roll the state can see.
8. **Update the transition table** in section 5 of this document and the diagrams in
   ARCHITECTURE.md.

Nothing in `game.c` changes, and neither does any existing state apart from the one that
triggers the new one.

## 10. Testing the state machine

The state machine is tested through the public API (`Game_Roll`, `Game_Score`), not by
calling states directly. So the tests check behavior, and they survive changes to the
states' internals: every refactoring in the git history kept them green. Each transition and
its tests:

| Transition or rule | Tests |
|---|---|
| `RegularFrame` takes a first roll | `should_get_a_score_of_0_from_an_incomplete_frame` |
| `RegularFrame` completes with two rolls under 10 | `should_get_a_score_of_8_from_a_regular_frame_rolls_2_6`, `should_score_correctly_with_a_gutter_ball` |
| A complete frame passes rolls on | `should_score_5_from_rolls_2_3_4`, `should_score_14_from_two_complete_frames_rolls_2_3_4_5` |
| `RegularFrame` becomes `SpareFrame` | `should_score_an_unfinished_spare_as_0` |
| `SpareFrame` takes its bonus, completes, passes on | `should_score_a_complete_spare_roll_8_2_1`, `should_score_a_complete_spare_and_complete_regular_roll_8_2_1_4` |
| `RegularFrame` becomes `StrikeFrame` | `should_score_an_unfinished_strike_as_0` |
| `StrikeFrame` takes two bonuses, completes, passes on | `should_score_a_complete_strike` |
| Strike after strike (default `pins_standing`) | `should_score_a_full_game_correctly`, `should_score_a_perfect_game_and_reject_a_13th_roll` |
| Tenth frame: `TenthStrikeFrame` keeps its fill balls | `should_score_a_tenth_frame_strike_whose_fill_balls_leave_pins_standing`, `should_score_a_perfect_game_and_reject_a_13th_roll` |
| Tenth frame: `TenthSpareFrame` keeps one fill ball | `should_give_a_tenth_frame_spare_exactly_one_fill_ball` |
| Tenth frame: open, then game over | `should_end_the_game_after_an_open_tenth_frame` |
| `RegularFrame`'s `pins_standing` | `should_reject_a_second_roll_that_knocks_down_more_pins_than_are_standing`, `should_reject_a_tenth_frame_second_roll_larger_than_the_pins_standing` |
| `TenthStrikeFrame`'s `pins_standing` | `should_reject_tenth_frame_strike_fill_balls_totalling_more_than_ten` |

`should_score_a_full_game_correctly` goes through almost every transition and checks the
running score after every roll.

## 11. Checking it as an explicit state machine

A common rule for embedded code: control logic that would otherwise depend on several flags
should be an **explicit, documented state machine** instead. A good explicit state machine
usually has three properties. Here is how this design measures against each:

| Property | Here |
|---|---|
| States and transitions are documented, as a table or diagram | Section 5 of this document, plus the diagrams in ARCHITECTURE.md |
| Each transition is a pure function of `(current state, event) → next state`, testable on the host | **Partly.** The guards (`RegularFrame_IsStrike`, `RegularFrame_IsSpare`) are pure functions of the frame and the roll, and every transition is host-tested (section 10). But the State pattern makes the transition *inside* the state's `roll()`, which also records the roll. So there is no single `next_state()` function to call on its own. That's the trade-off of the object-oriented form of the pattern, compared with the classic enum-plus-`next_state()` form |
| Undefined transitions have an explicit, safe default | Yes. Every state accepts every roll it can be given. Invalid rolls are rejected before any state sees them, a complete frame passes rolls on, and `RollList` refuses anything past its capacity (section 5, "What can't happen") |

## 12. What it costs

- **More code than a `switch`:** a header and a source file per state, a vtable each, `_Init`
  functions, the context's forwarding functions, and `(void)context;` in the states that
  don't use it.
- **Memory for every state in every frame:** each context holds 5 state slots of 16 bytes
  on a 64-bit host, and uses 1 or 2 of them in a frame's lifetime.
- **An indirect call per request,** through the vtable. It's cheap, but not free, and it makes
  the call graph harder for static tools (stack-depth analysis, for instance) to follow.
- **A dependency back from a state to the context,** because a state that changes state must
  know its context. It's kept narrow: `regular_frame.c` includes only `frame_transition.h`,
  three functions, and only `RegularFrame` uses it.
- **Discipline, not the compiler,** keeps "base struct first" and "states change `Frame`'s
  fields only through `Frame`'s functions".

In return, each state is short (the longest state function is 17 lines), testable through
the public API, and can be changed or added without touching the others.
