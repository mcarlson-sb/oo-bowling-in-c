# Toward Kay's OO: an experiment log

This branch (`kay-oo`) asks how far the scorer can honestly move from Simula/C++-style
objects (fixed vtables, a closed set of states, one hidden implementation behind `Game`)
toward Alan Kay's idea of object-oriented programming, which he summed up as:

> OOP to me means only messaging, local retention and protection and hiding of
> state-process, and extreme late-binding of all things.

It also asks where that stops paying off. The method is Kent Beck's: every structural change
must be pulled in by a failing test for a real feature ("make the change easy, then make the
easy change"), and each commit is tagged **make-easy**, **make-change** or **clean-up**. No
machinery is added ahead of a test that needs it.

## In short

- **Late binding paid off only where a feature crossed a boundary between owners:** a
  counting rule the caller owns (phase 1), listeners the caller owns (phase 3), and a game's
  history, which the caller may revise (phase 5). Across every phase, only `game.h` and
  `game.c` changed. The frame classes, the states and the context are exactly as `main` has
  them.
- **The costs landed at the same boundaries.** Every defect class the experiment found sits
  at one of them, and the closed interior never had any:
  - a buggy rule silently corrupting a game;
  - a `void *` cast the compiler can't check;
  - a call back into the game breaking the order of notifications;
  - two edits showing listeners a game that never existed.
- **Inside one owner's code, the closed design is simpler and fully checked.** Corrections
  even argued *against* objects there: once every roll is kept, data plus one function would
  do.
- **Going further in C means rebuilding a runtime by hand.** The `(callback, void *)` closure
  is the first piece. Message selectors, dispatch and `doesNotUnderstand` weren't built,
  because nothing needed them.
- **Tests settled what arguments couldn't.** Tests, probes and mutations overturned several
  confident arguments, the author's and the reviewer's:
  - the double-completion test didn't separate two designs;
  - a "reported frame can't vanish" argument was wrong;
  - the no-tap property had a hole of its own;
  - a scratch-copy design would have left dangling pointers.

The full conclusion is at the end.

For each phase this log records:
- which of Kay's three properties moved, and the evidence;
- what it cost;
- the strongest argument that the result still isn't Kay OO;
- whether a plain `switch` on an enum would be simpler.

Two further lenses are used throughout. **William Cook's distinction:** an *abstract data
type* hides one implementation behind a type, while *objects* are defined only by their
interface, so different implementations can work together. **Where C stops:** any place
where going further would mean rebuilding part of a language runtime (such as
`objc_msgSend`-style dispatch) is noted, not built.

---

## Baseline (`main`, before this branch)

| Property | State at the start |
|---|---|
| **Messaging** | None. Every interaction is a synchronous C call. Virtual dispatch through a vtable chooses *which* function runs, but it is still a procedure call, and the set of operations is fixed in `FrameVtable` |
| **Hiding of state-process** | Strong. `Game` is an opaque handle, the frame classes live in `src/`, and states change `Frame`'s fields only through `Frame`'s own functions |
| **Late binding** | Internal only. The vtable picks a state's code at run time, but the five states and the two factory families are closed at compile time. A caller can bind nothing |

In Cook's terms, `Game` is an **abstract data type**: one implementation, hidden behind a
type. The frames are closer to objects, since they interoperate only through `FrameVtable`,
but they are all the library's own, so no outside implementation ever meets them.

Size: 901 lines in `src/`, 55 in `include/`.

---

## Phase 1: nine-pin no-tap, supplied by the caller

**Feature.** In nine-pin no-tap, knocking down 9 on a first ball counts as a strike. The
library must not contain the rule; the test, acting as a client, supplies it.

### What the tests pulled in

| Commit | Tag | What |
|---|---|---|
| `Refactor: name the pins standing before a roll` | make-easy | `Game_Roll` already asked the latest frame how many pins were standing, but only inline, inside the validation check. Naming the value made room for a rule to use it |
| `Nine-pin no-tap: a caller-supplied rule counts a first-ball 9 as a strike` | make-change | `typedef Pins (*PinCountRule)(Pins pins_standing, Pins pins_down);` and `Game_CreateWithRule`. `Game_Roll` applies the rule after validating the roll and before any frame sees it |
| `...an earlier strike's bonus counts a no-tap 9 as 10` | make-change | Passed without new code |
| `...tenth-frame fill balls count a no-tap 9 as a strike` | make-change | Passed without new code |
| `Reject a roll that the game's rule counts as too many pins` | clean-up | A trust boundary the feature created (see Costs) |
| `Refuse to create a game without a rule` | clean-up | `Game_CreateWithRule(NULL)` returns `NULL` |

**Your guess, tested.** The guess was that the feature would push toward
`Game_Create(const Rules *rules)`, with a public protocol for making frames and for the frame
protocol itself, so that caller-supplied frames would work alongside the library's own. **The
tests didn't pull that in.** The simplest thing that passed was one function pointer, and
nothing the tests asked for needed more:
- The variant changes **how a roll is counted**, not how a frame behaves. So the rule could be
  applied once, at the door, and the whole frame machinery sees a no-tap 9 as a plain 10. The
  strike state, earlier frames' bonus rolls and the tenth frame's fill balls needed no change.
  The two "passed without new code" tests are the evidence.
- The frame classes stay private, and there's no `Rules` struct, because only one thing
  varies. A struct of one function pointer would be a container with nothing else to hold.

The guess would hold for a variant that changes a frame's *structure*, not its counting:
candlepin or duckpin, with three balls a frame, for instance. There, a caller would have to
supply frame behavior, and a public frame protocol would be pulled in. No feature here asks
for that.

### Kay's three properties

| Property | Moved? | Evidence |
|---|---|---|
| **Messaging** | No | Invoking the rule is a synchronous call through a function pointer. The caller's code runs; nothing is *sent* to anything that could decide how to respond |
| **Hiding of state-process** | Unchanged, slightly reinforced | The rule sees only two numbers, `(pins standing, pins down)`, never a frame. The library knows nothing of the rule but its signature |
| **Late binding** | Yes, a little | How a roll is counted is now bound at run time, by the caller, separately for each game. The library contains no no-tap code (`git grep -n "no-tap\|NinePin" src include` finds nothing), and the same compiled library plays both games |

**Cook.** `Game` is still an abstract data type. The rule is a *procedural abstraction*, known
to the library only by its behavior, and different implementations (standard, no-tap) coexist
in one program. So by Cook's definition the rule is an object. But it's the degenerate kind:
one method, no state, no identity. A C function pointer has no environment, so it can't even
be a closure. It's much closer to a first-class function than to anything Kay described.

### Costs

- **Size:** the library grew by 22 lines of code, about 40 with comments (`src/` 901 → 923,
  `include/` 55 → 70). The client test is 85 lines. A `Game` grew by 8 bytes (the rule
  pointer), from 968 to 976, which ARCHITECTURE.md only caught up with in phase 3.
- **Indirection:** one more call through a function pointer per roll.
- **Compile-time checking lost.** Before, the compiler could see every way a roll was
  counted. Now a count can come from anywhere, and its range became a run-time check:
  - A probe *before* the fix showed what a buggy rule (one that counts a strike as 11) did.
    Every roll was accepted, the pins still standing wrapped around to 255, and 10, 3, 4
    scored 14 where it should be 7.
  - UBSan saw nothing, because unsigned wrap-around is defined behavior in C.
  - The closed design never needed a check here, because the library wrote every count
    itself. Late binding *created* the gap, and `GAME_ERR_RULE_OUT_OF_RANGE` now closes it.
  - The library still can't stop a rule from being impure. A rule that reads a global could
    score the same rolls differently from one game to the next.
- **Fit with no heap:** perfect so far. A function pointer allocates nothing. A rule that
  needed *configuration* ("N-pin no-tap for any N") would need a context pointer (`void *`),
  and that would lose type checking again. Nothing needs it yet, so it isn't there.

### The strongest argument that this still isn't Kay OO

This is a **hook**, not an object you send messages to. There is one operation, its
signature is fixed at compile time, and it is bound once, when the game is created, and
never again. Nothing can respond to a message it wasn't compiled to expect, and there's no
receiver with an identity or state of its own. `Game` is as closed an abstract data type as it
was before. "Extreme late-binding of all things" has become "late binding of one thing".

### Would a `switch` be simpler?

**Yes, if the library were allowed to contain the rule.**
`enum GameVariant { GAME_STANDARD, GAME_NINE_PIN_NO_TAP }` plus a two-case `switch` in
`Game_Roll` would be simpler:
- there'd be no trust boundary, no new error code and no probe;
- every variant would be known to the compiler.

The function pointer only pays off because of the constraint that the *caller* owns the
variant. Given that constraint, it is the simplest design that meets it. Nothing smaller
works.

### A note for phase 2

This feature was easy *because* `Game` asks the latest frame how many pins are standing:
the rule needs that number. That's exactly the query phase 2 ("tell, don't ask") wants to
remove. If the newest frame validates rolls itself, the rule can no longer be applied at the
door by `Game`. The newest frame would have to apply it, so frames would need to know the
rule. That, not this feature, is what might finally pull in a `Rules` object that frames
consult. Whether it should is phase 2's question.

---

## Phase 2: tell, don't ask. Not done, deliberately

**The brief.** Remove `Game`'s queries about pins standing and whether a frame is complete.
Give each roll to the newest frame first; that frame validates the roll itself and passes
bonus pins back to the frames before it, and completed frames ask their `Rules` for the next
frame. Do it only if phase 1's clean-up makes it natural; otherwise explain why.

**Decision: not done.** It isn't natural, and nothing pulls it in:

- **No failing test asks for it.** Every behavior in the suite already works, so this would
  be a restructuring with no feature behind it. That's exactly the machinery-ahead-of-need
  that Beck's discipline rules out.
- **Phase 1 moved the other way.** `Game_Roll` now uses the answer to "how many pins are
  standing?" three times: to validate the roll (`pins > pins_standing`), as input to the
  caller's rule (`count_pins(pins_standing, pins)`), and to check the rule's result
  (`pins_counted > pins_standing`). The no-tap feature was easy *because* `Game` can ask that
  question at the door. Removing the question means moving the rule into the frames.

### What it would have taken

Measured against the current code, telling instead of asking would change every module
except `RollList` and `SlotPool`:

| Change | Where | Why |
|---|---|---|
| `RollResult` gains a third outcome, *rejected*, with a reason | `frame.h`, every state | If the newest frame validates, it must be able to say no |
| All five states' `roll()` validate their own roll and apply the rule | `regular_frame.c`, `strike_frame.c`, `spare_frame.c`, `tenth_frame.c` | Validation moves from `Game` into the frame that knows its rack |
| The game's rule reaches every frame | `FrameContext_Init`, and the `roll()` signature or `frame_transition.h` | The frame that keeps the roll now has to count it |
| Rolls travel newest first, then *back* to earlier frames | `game.c`, or a link from each frame to the one before | Either `Game` still walks the frames (so it's still orchestrating), or each frame points at its predecessor: the kind of back-pointer that smell #5 removed |
| "Completed frames ask their `Rules` for the next frame" | a new `Rules` factory with access to `Game`'s frame storage and the frame number | Creation moves out of `Game`. The rules object would need the storage, and to know which frame is the tenth. That is the public frame protocol phase 1's guess predicted, now pulled in by a principle, not a feature |
| Undo a rejected new frame | `game.c` | Today "a rejected roll changes nothing" holds because every check runs before any frame acts. Newest-first still gives that for most rolls, but a roll that opens a new frame and is then rejected by it would need the new frame taken back |

**What it would buy.**
- `Game` would stop enforcing a frame's rule itself. The `pins > pins_standing` check is
  mild feature envy: `Game` using a frame's data to make the frame's decision.
- `pins_standing` could become private to the states.

Both are real improvements, but small ones. Neither changes any behavior a test can see.

**What feature would pull it in.** One whose validity can't be expressed as "no more than the
pins standing": a validity rule only the frame can know. The strong example is a
caller-supplied frame type that changes a frame's *structure*, such as candlepin's three balls
a frame. Then `Game` couldn't ask a question with a fixed answer shape, and telling the frame
would be the only honest design. No such feature is in this brief.

*Corrected after review:* an earlier draft also gave fouls as an example. That was wrong, and
untested. A foul counts as 0 and still uses up a ball, and a rule at the door can do that if
the caller's input says "foul". It changes the API, not where the rule lives.

### Kay's three properties

Nothing moved: no code changed in this phase.

Had it been done, **messaging** would have moved a little in spirit, with `Game` telling where
it now asks. It would still be synchronous C calls with a fixed signature, so not messages in
Kay's sense. **Hiding** would have improved slightly (`pins_standing` private to the states).
**Late binding:** no change.

### Costs

None this phase. The tests, the build and the design are unchanged, and nothing in the
README or ARCHITECTURE.md needed updating.

### The strongest argument against stopping here

Tell-don't-ask is a sound principle, and `Game` *is* making a frame's decision with a frame's
data. Declining it leaves a real, if small, smell. The counter-argument: fixing that smell
means reshaping the core protocol (a rejection outcome, the rule inside the frames, a reversed
roll order, and frame creation moved out of `Game`) with no test that fails today and passes
afterwards. Here the principle and the discipline point different ways, and the brief puts the
discipline first.

### Would a `switch` be simpler?

Not the relevant question this phase: the asking design is already the simpler one. The
comparison worth recording is that the *current* code (`Game` asks the newest frame, then
decides) is simpler than tell-don't-ask would be, for the features we have.

### A note for phase 3

The live scoreboard needs to know *when a frame completes*. Today only the frame knows that,
at the moment it happens: inside a state's `roll()`, when it calls `Frame_Complete`. `Game`
could detect it after the fact by comparing frames' `complete` flags before and after each
roll, but that's asking again. So phase 3 may pull in the "frames tell" direction on its own:
a frame, or its context, telling an observer that it completed. Phase 2 declined to force
that shape; if phase 3 needs it, the test will say so.

---

## Phase 3: a live scoreboard, then a second subscriber

**Feature.** First, one observer told when a frame completes, using the simplest thing that
works. Then a second, independent subscriber, running stats. A message or event abstraction
is allowed only now, and only if the duplication between the two calls for it.

### What the tests pulled in

| Commit | Tag | What |
|---|---|---|
| `Live scoreboard: tell one callback when a frame completes` | make-change | `Game_OnFrameCompleted(game, callback, context)`. After each roll, `Game` reports every newly completed frame's number and score |
| `...a roll that completes no frame tells nothing` | make-change | Passed without new code |
| `...one roll that completes two frames tells both, oldest first` | make-change | Passed without new code; a mutation check proved it can fail |
| `...the tenth frame is told only after its fill balls` | make-change | Passed without new code |
| `...setting a callback on a NULL game is ignored` | clean-up | Test-first. The guard had been written early and taken out again, because no test asked for it |
| `Refactor: the frame-completed callback becomes a one-slot listener list` | make-easy | No behavior change; makes room for a second subscriber |
| `Running stats: a second, independent subscriber hears every completed frame too` | make-change | Two slots, and adding appends |
| `Running stats: a subscriber past the two slots is refused` | make-change | Closes the out-of-bounds write the previous step left open |
| `Running stats: under no-tap, it averages counted scores` | make-change | Carried over from phase 1's review. Passed without new code |
| `Refuse a NULL frame-completed callback` | clean-up | Stops a null listener wasting a slot, so the reporting loop needs no `NULL` check |
| `Refactor: shared test support for the black-box tests` | clean-up | `GameHandle`, `RollAll` and the no-tap rule had been copied into two or three test files each |

Two carry-over tests from phase 1 came first:
- 5 then 4 is **not** a spare under the client's first-ball rule.
- A stricter client rule, "any ball that leaves one pin clears the rack", **does** make it a
  spare, with no library change. The rule at the door covers both common no-tap variants.

### Diff in `Game`, or frames telling: what the tests said

All four required tests were written before choosing either design:
- one frame completes;
- a roll completes nothing;
- **one roll completes two frames**: a strike in frame 8, then 3 and 4 in frame 9;
- the tenth frame's fill balls.

Both designs were judged against all of them.

| | Diff in `Game` | Frames tell |
|---|---|---|
| Passes all four tests? | Yes | **Yes, by reasoning; not built** |
| Order for the double completion | `Game` reports in frame order | Also correct: the roll already travels oldest first, so frame 8 would announce before frame 9 |
| Where the observer lives | `Game` only | Every one of the ten contexts |
| When a listener is called | After the roll has gone all the way through | Mid-roll: frame 8 would announce before frame 9 has seen the 4 |
| Code touched | `game.c` | `frame_context`, and the path from `Frame_Complete` to it |

**The double-completion test didn't decide it.** The guess going in was that frames telling
would have to work to get the order right across the chain. It wouldn't: the order comes
free. Both designs pass, and what separates them is **coupling and timing**. Diff in `Game`
won: no frame holds an observer, and no listener can look at a half-applied roll. The
timing point is reasoning, not a test. A listener that reads the game from inside its
callback would show it, but no feature here needs such a listener.

"Diff" ended up even simpler than comparing every frame's flag before and after. A frame
never completes before the one before it, so `Game` keeps a single count of frames already
reported, and checks only the frames just past it. The double-completion test pins down that
bowling fact: with `while` mutated to `if`, it, and only it, failed.

### Did an event abstraction appear?

**No, and the duplication didn't call for one.** Both subscribers need the same thing (a
frame's number and its score), through the same signature. The only duplication was
*calling* two subscribers in place of one, and a loop over a two-slot array removed it. An
event type or message struct (a tag plus a payload) would earn its place only with a second
*kind* of event, and no test asks for one.

### The carried-over question: counted pins or the pins that fell?

Under no-tap, notifications carry **counted** values: a first-ball 9 scores as a strike. The
stats subscriber averages frame scores, and a league average is score-based, so counted values
are exactly what it needs. The test (9, 3, 4 gives frames of 17 and 7) passed without new
code. **No pressure yet to move the rule past the door.** A pinfall statistic (the pins that
actually fell) would be the first. No such subscriber is in this feature.

### Kay's three properties

| Property | Moved? | Evidence |
|---|---|---|
| **Messaging** | Yes, the most so far | For the first time, `Game` sends information to receivers it knows nothing about. It holds a function and an opaque `void *`, never the scoreboard or the stats. Each receiver decides what the news means. It is still a synchronous call with one fixed signature, though |
| **Hiding of state-process** | Yes | Listeners receive a frame number and a score, never a frame. Their own state (`Scoreboard`, `RunningStats`) stays theirs: *local retention* of state, in client objects `Game` can't see into |
| **Late binding** | Yes | Who hears about completed frames is bound at run time, per game, by the caller. The library has no idea a scoreboard or stats keeper exists |

### Costs

- **Size:** the library grew by 41 lines of code, 63 with comments (`src/` 923 → 973,
  `include/` 70 → 83). A `Game` grew by 48 bytes, to 1,024 (compiler-checked).
- **Indirection:** after a roll, one indirect call per listener per completed frame.
- **Compile-time checking lost:**
  - The `void *context` must be cast back by the subscriber. A wrong cast compiles cleanly and
    is undefined behavior.
  - This is the first place C makes us **hand-build part of a language runtime**. The
    `(callback, void *context)` pair is a closure put together by hand, because C function
    pointers carry no environment. Noted, not generalized.
- **Fit with no heap:** the subscriber set is fixed at two slots, so "subscribe" can now fail,
  and the API had to report that (`bool`).
- **What was held back:**
  - An early `NULL` guard, written ahead of its test, was taken out and re-added test-first.
  - The first two-slot version left a third subscription writing past the array for one
    commit, and the next test closed it.

### The strongest argument that this still isn't Kay OO

A callback is a **procedure pointer with one fixed signature**, not a message:
- There is exactly one kind of notification, and a receiver can't decline it, answer it, or
  be sent anything else.
- It's synchronous, so `Game` waits while each listener runs.
- The receiver has no identity `Game` could address. It's a `(function, void *)` pair that C
  forced us to assemble by hand.
- The set of receivers is capped at two by the no-heap design.

Messaging moved further than in any phase so far, but it's a narrow, typed pipe, not Kay's
open-ended conversation between objects.

### Would a `switch` be simpler?

**Not here, because what varies is the receiver, and the library can't know its receivers.**
- A `switch` needs every case in the library, and the scoreboard and stats live in the
  caller's code.
- If the only listeners were the library's own (say, a built-in statistics module), plain
  direct calls would be simpler than callbacks.

The callback earns its place only because the subscribers belong to the caller.


### A note for phase 4 (written before the review)

Phase 4, a receiver that can decline a message it doesn't understand (`doesNotUnderstand`),
is optional, "only if features 1 to 3 leave a real need". So far nothing does:
- There is one notification, and every listener wants it.
- There is one rule, and every game needs one.

A need would first appear with a second kind of notification that some listeners don't care
about. Even then, the simplest answer is probably a separate subscription per kind, not a
generic receiver that can decline. The phase 4 review should confirm that before anything is
built.

### After review: calling back into the game from inside a listener

The review asked for the mid-roll argument to become a test. Messaging brings a hazard the
closed design never had: a listener can call back into the game while it is being told.

| Commit | Tag | What |
|---|---|---|
| `Live scoreboard: a listener that reads the score sees the whole roll` | make-change | Passed without new code |
| `Refuse a roll made from inside a frame-completed listener` | make-change | New `GAME_ERR_ROLL_DURING_NOTIFICATION` |

**Reading the score inside a callback.** When one roll completes frames 8 and 9, a listener
that calls `Game_Score` must see the score after the whole roll, both times: `{24, 24}`.
- The diff-in-`Game` design passes, because it reports only once the roll has gone all the
  way through.
- To see whether the test separates the two designs, a mutation reproduced frames-telling
  timing, reporting inside the chain loop right after each frame takes the roll. This test,
  and only this test, failed. It saw `{17, 24}`: frame 8's notification arrived before frame 9
  had taken the 4.

So phase 3's timing argument is no longer just reasoning: this test is what separates the
designs, where the double-completion test didn't.

**Rolling inside a callback.** Before deciding, a probe showed what the code did on its own.
A listener that rolled a 4 from inside frame 1's notification:
- was **accepted** (`GAME_OK`);
- completed frame 2 **in the middle of frame 1's notification**;
- so the other listener heard `(2, 7)` **before** `(1, 13)`, silently breaking the
  oldest-first promise in `game.h`.

It was the same kind of hole as phase 1's rule probe: no build and no sanitizer noticed. There
were two ways to fix it:
- **Allow it and keep the order.** That needs a queue of rolls held back until notification
  ends, fixed-size because there's no heap. That's machinery no feature asks for.
- **Refuse it.** One flag, set while the game is notifying.

**Refused.** `Game_Roll` from inside a callback returns `GAME_ERR_ROLL_DURING_NOTIFICATION`
and changes nothing; the same roll made after the callback returns is fine. `game.h` now says
what a callback may do: read the game, not roll it.

**Still untested, and recorded here, not fixed:** a listener could also call `Game_Destroy`
or `Game_OnFrameCompleted` from inside its callback. Destroying the game would leave `Game`
reporting on a released slot. Subscribing would add a listener to the list being walked. No
feature needs either, and the same one-flag answer would cover both if one ever did.

---

## Phase 4: `doesNotUnderstand`. Skipped, deliberately

**The brief.** Optional, and only if features 1 to 3 leave a real need: a receiver that can
decline a message it doesn't understand, as Smalltalk's `doesNotUnderstand` allows.

**Decision: not built. Nothing needs it.**
- There is **one** kind of notification, and every listener wants it.
- There is **one** rule, and every game needs exactly one.
- No receiver is ever sent something it wasn't written to handle.

A need would first appear with a second *kind* of notification that some listeners don't care
about. Even then, the simplest answer is a separate subscription per kind, so that no
listener is ever sent the message. A generic receiver that can decline is heavier. In C it
would also mean building a message selector and a dispatch step: exactly the
`objc_msgSend`-style runtime this experiment said it would note rather than build.

---

## Phase 5: correcting a roll

**Feature.** The scorer entered 7, but the bowler knocked down 8: fix the roll and rescore
everything after it. The review predicted this would make each roll a thing in its own right
(a message kept as data, not just a call), and would test the phase 1 prediction that
replaying needs the pins that actually fell. Two decisions were the user's:
- A correction that makes a later roll impossible is **rejected**.
- A correction that reopens a completed frame is told with **one "frame changed" message**.

### What the tests pulled in

| Commit | Tag | What |
|---|---|---|
| `Refactor: separate accepting a roll from telling the listeners` | make-easy | `Game_Accept`: check and apply a roll, telling no one, so it can be replayed |
| `Corrections: rescore the game when a roll is corrected` | make-change | A log of every accepted roll; `Game_CorrectRoll` swaps one entry and replays |
| `...reject a correction that makes a later roll impossible` | make-change | The user's policy. The old roll is put back and the original log replayed |
| `...every replayed roll is counted again from the pins that fell` | make-change | Passed without new code; a mutation proved it guards the prediction |
| `...re-send the rescored frames to the listeners` | make-change | The same message, with a repeated frame number as an update |
| `Refactor: move RunningStats into the shared test support` | make-easy | So the correction tests use phase 3's subscriber unchanged |
| `...running stats keep their numbers right across a correction` | make-change | The subscriber changed, not the library |
| `Refactor: frame-completed becomes frame-changed, with a completion flag` | make-easy | The user's choice; no behavior change |
| `...tell the listeners when a correction reopens a frame` | make-change | A reopened frame is sent with `complete = false` |
| `...reject a roll number the game hasn't had`, `...NULL game`, `...from inside a listener` | clean-up | The edges of the new call |
| `...a property test that listeners match a fresh game` | clean-up | 3,000 random corrected games against fresh ones |

### Messages as data

A roll used to disappear into frame state the moment it was accepted. Now the game keeps
every roll, as the pins that fell, and can go back and replay them. That's the review's "the
message is a thing" point, pulled in by a feature.

**The phase 1 prediction, tested two phases later.** Replaying applies the caller's rule
again, so the log must hold the pins that fell, not what they were counted as. Under no-tap,
the rolls 10, then 9 (counted as a strike), then 3, with the first roll corrected to 1, must
turn the 9 into a spare. That test passed without new code, because storing the rolled pins
was also the simplest thing to do. A mutation that logs the counted value made that test, and
only that test, fail: the replay then tries to knock down 10 pins with 9 standing.

### "Rescored": one message, or two?

As the review suggested, the existing message was tried first: re-send every complete frame,
and a listener treats a repeated frame number as an update.
- **The keyed scoreboard** needed no change.
- **Phase 3's running stats** failed, as predicted. It counted every message, so after a
  correction it had 4 frames and an average of 7.5, where it should have had 2 and 8.
- **The fix was in the subscriber, not the library.** Stats now keeps the latest score for each
  frame. It never genuinely had to tell an update from a new frame; it only needed state keyed
  by frame number.

Then a case the review didn't raise: **a correction can reopen a completed frame.** The scorer
entered 3, 4, but the first ball was a strike, and frame 1 now waits for a second bonus roll.
A probe showed both listeners kept believing frame 1 was complete, with 7. Re-sending
*complete* frames can't take anything back.

The user chose **one "frame changed" message**, carrying the number, the score and whether the
frame is complete. A reopened frame is sent with `complete = false`. It is still one kind of
message: it describes a frame's state, not an event. So phase 4 stays skipped, now with
evidence: no receiver ever needed to decline a message, and no second *kind* was needed. But
every subscriber had to change, because the callback's signature changed and each subscriber
must now drop a frame that reopens.

### A property test, and a wrong assumption

Coverage showed one branch never taken: a frame the listeners had been told about that no
longer exists after the replay. I reasoned that a one-roll correction couldn't cause that. A
property test settled it. Across 3,000 random games (fixed seed), each corrected at a random
roll, a corrected game's listeners must end up exactly where a fresh game's would, fed the
corrected rolls. The test holds, and it reaches that branch: **the reasoning was wrong, and the
defensive code was needed.** It's the second time in this experiment that a test overturned an
argument; the first was phase 3's double-completion guess.

### Kay's three properties

| Property | Moved? | Evidence |
|---|---|---|
| **Messaging** | Yes, in a new direction | A roll is now kept and replayed as data. The notification became a *state update* that is safe to repeat, not a one-off event, which suits a receiver that may hear the same thing twice |
| **Hiding of state-process** | Unchanged | The log is private to `Game`; listeners still see only numbers |
| **Late binding** | No | Nothing new is bound at run time |

### Costs

- **Size:** the library grew by 90 lines of code and lost 20, with the rename (`src/` 980 →
  1,065, `include/` 88 → 103).
- **Memory, checked with `offsetof` and `sizeof`:** the log adds 22 bytes per game (21 rolls
  and a count), but a `Game` grew by only 16, from 1,024 to 1,040 bytes. At the end of phase 3
  the data ended at byte 1,018 and the struct was padded to 1,024; the log starts at offset
  1,018 and ends at exactly 1,040, so 6 of its 22 bytes went into that padding. The pool of two
  games is 2,080 bytes. (Earlier summaries said "1,024 in all" and "1,040 in all"; both meant
  one `Game`, not the pool.)
- **Time:** a correction replays up to 21 rolls through up to 10 frames. That's negligible
  here, but it's proportional to the game so far, where a roll is constant time.
- **A new requirement the compiler can't check: the rule must be pure.** Restoring a rejected
  correction replays the original log and trusts that the result is the game as it was. A rule
  that reads global state could replay differently. A debug build's `assert` catches only a
  replay that *fails*, not one that silently differs.
- **Cost moved to the subscribers:**
  - The callback's signature changed.
  - Every subscriber must be safe to tell twice (keyed by frame number) and must handle
    `complete = false`.
  - A subscriber whose effect can't be undone (one that rings a bell for each strike, or
    appends to a paper log) couldn't follow corrections at all with this message. It would
    need a separate message that says "this is a correction". No such subscriber exists here.

### The strongest argument that this still isn't Kay OO

The "message as data" lives *inside* `Game`: a private array of bytes that only `Game` ever
replays, never sent to anything else. The notification is still one synchronous callback with a
fixed signature. And "a repeated frame number is an update" is a convention every subscriber has
to know and follow; nothing enforces it.

### Would a simpler design do?

**Here the honest comparison isn't a `switch`: it's the procedural kata.** Once the game keeps
every roll, the score could be *computed* from the rolls by one pure function, as the classic
procedural version does, and a correction would be trivial: change the array, compute again.
Keeping the rolls makes the incremental frame objects into a cache that has to be rebuilt after
every correction. For this feature alone, data plus a function would be simpler than objects.
The State pattern still earns its place in validating rolls as they arrive and in telling
listeners *as* frames complete. But corrections are the first feature that argues *against*
the object design, not for it.

### After review: four checks

| Check | Result |
|---|---|
| **Does a rejected correction tell the listeners anything?** | No. Both replays go through `Game_Accept`, which tells no one. A test pins that down, and a mutation that reports the failed replay made it, and only it, fail. The property test now also checks the 500+ rejected corrections it generates: listeners and score must be unchanged |
| **Is purity only needed for restoring a rejected correction?** | No. *Every* correction replays every roll through the caller's rule, so an impure rule can rewrite history on any correction. The contract is now written on `PinCountRule` in `game.h`, where a caller writing a rule will see it |
| **Do the memory numbers add up?** | Yes, checked with `offsetof` and `sizeof` (see Costs above): 6 of the log's 22 bytes went into padding that already existed |
| **The longest game, 21 rolls: correct roll 21 and roll 1** | Both work, roll 22 is refused, and the game stays over. An off-by-one mutation of the bounds check made this test, and only this test, fail. **The upper edge had no test guarding it before** |

The review also asked for CI. GitHub Actions now runs the debug, release and UBSan builds on
Ubuntu with its own GCC, and the first run passed all three. That's the same code, warning-free,
on a second compiler and a second platform.

---

## Phase 5b: after a code review of phase 5

The first review of the actual commits, not summaries, found three gaps and two small
things. Each was settled by a test or a probe.

### 1. A correction couldn't change how many rolls there were

`Game_CorrectRoll` replaced one roll, but the most common scoring mistakes change the count:
- **A tenth frame entered as 10, 0, 0 that was really 9, 0.** Correcting roll 19 to 9 was
  rejected, because the third ball never happened but was still in the log.
- **A strike that was really 9 then 1.** Correcting the 10 to a 9 made the 3 the frame's
  second ball (9 + 3 > 10), so it was rejected.

**One edit, or two calls? The test decided.** The requirement was the reviewer's: after the
tenth-frame fix, the listeners hear only the final state. A probe gave the two-call design a
real, temporary `Game_DeleteRoll`, and tried both orders:
- **Correcting first** was rejected. The delete then left the tenth frame open, and the score at 0.
- **Deleting first** reached the right score, but the listeners first heard `(10, 0, open)`: a
  tenth frame waiting for a fill ball, a game that never existed.

So the design is one call, `Game_EditRolls(game, first_roll, rolls_removed, new_pins,
new_count)`, which replaces a range of rolls with new ones. Replacing, inserting and deleting
are all that one edit, and `Game_CorrectRoll` became a one-line wrapper. Its edges came with
tests:
- **More than 21 rolls after the edit** is refused before anything is written. Without the
  check, the debug build silently wrote a 22nd roll past the log, and the UBSan build caught it.
- **A `NULL` list of new rolls** is refused.

### 2. "Replay into a scratch copy" would have left dangling pointers

The review suggested undoing a rejected edit by replaying into `Game scratch = *game` and
copying it back only on success. A probe did exactly that: after the copy-back, **every
frame's state pointer pointed outside the game**, 1 of 1 frames after a simple correction and
10 of 10 in the longest game. Each `FrameContext` points at one of its *own* state slots, so a
copied game's pointers still point into the copy, a stack frame that is about to be gone.

**That is an object/value distinction the code had been making all along without saying so.**
The frames are genuine objects: they have identity, and a copy isn't the same thing. The roll
log is a value. So the rule became *copy the value, never the object*:
- The log is now a `RollLog` value type: 21 pins and a count, 22 bytes.
- A rejected edit copies the saved log back and replays it, one restore path for every kind
  of edit. That was the user's choice over validating in a scratch copy, which would only have
  moved the dependence on a pure rule to accepted edits, and added a replay.

### 3. The property test only covered the standard rule, and was incomplete

Running the correction property under no-tap was meant to cover the rule path. It showed that
**the property itself had a gap.** The mutation that logs counted values didn't make the no-tap
property fail. With such a log, some corrections that should be accepted are rejected. The
property only checked that a rejected correction changes nothing, never that a correction is
accepted when it should be.
- **The fix:** the fresh game is now the judge of acceptance too. The edited game must accept
  exactly when a fresh game accepts all the edited rolls.
- **The result:** the mutation now fails the no-tap property. The standard property can't
  catch it, because under the standard rule counted and fallen pins are the same.
- **Random range edits** are now property-tested the same way, under both rules. That covers
  replacing, inserting, deleting, appending, and deleting every roll.

### 4. Two small things

- **The chain loop.** Its guard, and the comment defending against "a future state", were
  code for a situation no feature has. A kept roll now simply ends the loop. One branch of the
  loop condition is still never taken, because only the latest frame keeps a roll. The README
  records that as a bowling fact, not as a defense.
- **A summary at the top of this log**, for teaching.

### Kay's three properties

| Property | Moved? | Evidence |
|---|---|---|
| **Messaging** | Yes | An edit is one message that describes a whole change, and listeners only ever hear its final state. The two-call probe shows what a message split in two costs: a moment in which the receivers see a game that never existed |
| **Hiding of state-process** | Clarified | The probe showed the frames have identity: a `FrameContext` refers to itself, so it can't be copied. That is what makes them objects and not values. The design now keeps the two apart |
| **Late binding** | No | Nothing new is bound at run time |

### Costs

- **Size:** 55 lines of library code added and 25 removed (`src/` 1,065 → 1,097, `include/`
  103 → 124). The API is one function larger, but `Game_CorrectRoll` shrank to one line.
- **Memory:** unchanged. `RollLog` is the same 22 bytes as the separate array and count it
  replaced, and a `Game` is still 1,040 bytes (both compiler-checked).
- **Stack:** an edit keeps two logs on the stack for a moment, the saved one and the edited one:
  44 bytes. Validating in a scratch copy of the whole game would have needed about 1 KB.
- **Still a trust boundary:** replay still depends on the rule being pure, now for any edit.
  That contract is written on `PinCountRule` in `game.h`.

### The strongest argument that this still isn't Kay OO

The edit is a *function call with an array argument*, not a message object that could be
stored, forwarded or sent to more than one receiver. "One edit is one message" is true at
the level of *this* API's meaning, not in how C calls it.

---

## Conclusion: late binding pays at the boundaries between owners

Across the phases, Kay's properties moved **only where a feature crossed a boundary between
owners**:
- a counting rule the caller owns (phase 1);
- listeners the caller owns (phase 3);
- a game's history, which the caller may now revise (phase 5).

Inside the library, where one party owns everything, the closed Simula-style design held up
against every test. The evidence is in the diff: **of the 20 library files, the experiment
changed only `game.h` and `game.c`**, the boundary. The frame classes, the states, the context,
`RollList` and `SlotPool` are untouched since `main`. Phase 2, the one attempt to push late
binding *inward* ("tell, don't ask"), had no feature behind it and was declined.

The cost landed in exactly the same place. Every defect class the experiment found or
guarded against sits at one of those boundaries, and the closed interior never had any of
them:

| Boundary | What late binding cost there |
|---|---|
| The caller's rule | A buggy rule silently corrupted a game (the pins-standing wrap to 255); a `NULL` rule would crash. Now `GAME_ERR_RULE_OUT_OF_RANGE`, and `NULL` refused |
| The caller's listeners | A wrong cast of the `void *` context is undefined behavior no build can catch; a `NULL` callback wasted a slot; a third subscriber overflowed the array for a commit; a roll from inside a listener broke the ordering promise. Now refused, refused, refused, and `GAME_ERR_ROLL_DURING_NOTIFICATION` |
| The caller revising history | A roll number the game hasn't had would have written far past the log; a correction from inside a listener would have replayed mid-notification; a correction can reopen a frame the listeners were told was complete; restoring a rejected correction trusts that the rule is pure. Now `GAME_ERR_NO_SUCH_ROLL`, refused, a `complete = false` message, and a documented requirement the compiler can't check |

**The teachable conclusion: use late binding at the boundaries between owners, where it pays
for itself, and nowhere else.** Where the caller owns a variation, a function pointer or a
callback is the simplest honest design, and a `switch` can't do the job. Inside one owner's
code, the closed design is simpler, safer and fully checked by the compiler. Every boundary
you open also needs guarding: the new checks all appeared right there, and none was needed
anywhere else.

**One counterpoint, from phase 5.** Keeping every roll as data makes the score computable by one
pure function, as the procedural kata does, and a correction would then be trivial. The
incremental frame objects become a cache that has to be rebuilt. Corrections are the one
feature that argued *against* objects inside the library, which fits the conclusion: inside
one owner's code, the simplest design wins, and here that is data plus a function.

What the experiment did **not** reach is Kay's OO proper: messages as first-class things,
receivers that can decline, and binding that stays open while the program runs. Each would
have meant rebuilding part of a language runtime in C (message selectors, dispatch,
`doesNotUnderstand`). No feature asked for that, so it wasn't built.
