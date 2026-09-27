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
  counting rule the caller owns (phase 1), listeners the caller owns (phase 3), a game's
  history, which the caller may revise (phase 5), and a machine on another thread and a
  display on the far side of a wire (phase 6). Of `main`'s library files, only `game.h` and
  `game.c` changed; phase 6 added the pinsetter beside them. The frame classes, the states
  and the context are exactly as `main` has them.
- **Phase 6 made messages asynchronous, and data.** The pinsetter posts a roll and doesn't
  wait; the main loop delivers it later. The remote scoreboard rebuilt itself from bytes with
  no change to the game, because phase 5 had already made the message describe a state. A
  second mailbox, inside the game, was added on principle and taken back out by the method:
  after phase 6, `game.c` is the 344 lines it was before it.
- **The clean-up after phase 6 took two reasons to change out of `game.c`**, the listeners and
  the roll log, with no change in behavior: 344 lines to 280. It cost 8 bytes of RAM a game,
  and it found one stack increase that step-by-step measuring had missed.
- **The costs landed at the same boundaries.** Every defect class the experiment found sits
  at one of them, and the closed interior never had any:
  - a buggy rule silently corrupting a game;
  - a `void *` cast the compiler can't check;
  - a call back into the game breaking the order of notifications;
  - two edits showing listeners a game that never existed;
  - a roll the machine reported, dropped silently (now kept until the scorer resolves it);
  - memory ordering between two threads (ThreadSanitizer catches each ordering weakened);
  - a second producer on the pinsetter, which loses rolls and which ThreadSanitizer never
    caught, even at 800,000 posts. Now a debug build stops if two posts overlap: a net, not
    a proof.
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
  - a scratch-copy design would have left dangling pointers;
  - ThreadSanitizer, expected to catch a second producer, never did;
  - a capacity change silently stopped the thread test from testing what it said it did.

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
  replacing, inserting, deleting, appending (wrongly, see the review below), and deleting
  every roll.

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

### After review: one hole, and one status doing two jobs

- **An edit could add rolls after the last one.** On an empty game, editing at roll 1 and
  inserting 3, 4 returned `GAME_OK`. So did inserting at roll 3 after two rolls. The range
  check allowed `first_roll` to be one past the last roll. That made the edit a second way to
  roll, and one the listeners heard as a correction, every frame sent again, and not as a
  roll. An edit now has to start at a roll the game has had. A forgotten last roll is a
  `Game_Roll`.
  - **The property test had the bug built in.** It picked edit positions up to and including
    one past the last roll, and called that "appending". Once the fix was in, it failed: the
    fresh game accepted rolls that the edit now rejected. The property now has one rule the
    fresh game can't judge: an edit past the last roll is rejected.
- **`GAME_ERR_GAME_OVER` meant two things:** "the tenth frame is complete" and "this edit
  would make more than 21 rolls". A caller could only tell them apart by knowing which call
  it had made. More than 21 rolls is now `GAME_ERR_TOO_MANY_ROLLS`, added at the end of the
  enum so the values that already existed keep their numbers.
- **The copy trap is now in `STATE_PATTERN.md` too** (section 4.3), where a learner reading
  about the context will find it.

---

## Phase 6: rolls from a pinsetter, scores to a remote scoreboard

**Feature.** In firmware, the pinsetter (the machine at the end of the lane that counts the
pins) reports each roll from an interrupt handler, at any moment. The main loop may be in the
middle of anything then, even telling the listeners about the roll before. And a scoreboard
may be on the far side of a wire. Two things were asked for: rolls that arrive through a
mailbox, and a remote scoreboard that gets only bytes. The prediction for the second was that
it would need no change to `game.h` or `game.c`.

### What the tests pulled in

| Commit | Tag | What |
|---|---|---|
| `Mailbox: a roll from inside a listener is queued until the notification ends` | make-change | `GAME_QUEUED`, and a mailbox in `Game`: phase 3's refusal becomes a queue. The re-entry test that pinned the refusal now pins the order |
| `Mailbox: refuse to queue more rolls than the game has left` | make-change | Without it, the 22nd queued roll was written past the mailbox, and UBSan trapped |
| `Mailbox: a queued roll that turns out impossible is dropped, and the rest still apply` | clean-up | Passed without new code. See the user's decisions for why the pinsetter doesn't do this |
| `Refactor: GAME_ERR_ROLL_DURING_NOTIFICATION becomes GAME_ERR_EDIT_DURING_NOTIFICATION` | clean-up | Only edits are refused now. Same enum slot |
| `Pinsetter: posted rolls go into the game, in order, when the main loop drains them` | make-change | `pinsetter.h`: `Pinsetter_Post` for the interrupt side, `Pinsetter_Drain` for the main loop. The pinsetter never holds a game |
| `...rolls still waiting apply after a correction made meanwhile` | clean-up | Passed without new code: edits never wait in a mailbox |
| `...refuse a post when the mailbox is full` | make-change | Never overwrite a roll the main loop hasn't seen |
| `CI: a ThreadSanitizer build, OO_C_TSAN, run on Linux` | make-easy | |
| `...rolls posted from another thread reach the game, in order` | make-change | A real thread plays the interrupt handler. The ring's counts become C11 atomics |
| `Remote scoreboard: a decoder rebuilds the scoreboard from the listener's bytes alone` | make-change | Passed without new code |
| `Pinsetter: stop at an impossible roll and keep it until the scorer resolves it` | make-change | The user's policy. `Pinsetter_Drain` returns a `GameStatus` |
| `...let the scorer discard a roll that really was a glitch` | make-change | `Pinsetter_DiscardOldest` |
| `...rolls made after the game is over wait for the next game` | make-change | Passed without new code |
| `Refactor: the pinsetter's ring keeps two positions, not two counts, so any capacity works` | make-easy | Argued, not tested: see the review |
| `...the mailbox holds a whole game's rolls, 21, while a drain is stopped` | make-change | The user's sizing. `GAME_MAX_ROLLS` moves to a private header, `game_limits.h`, and a static assert ties the capacity to it |
| `...count the rolls lost while the mailbox was full` | make-change | The user's choice. `Pinsetter_RollsLost` |
| `...the lost-roll count stays right when it wraps around` | clean-up | Passed without new code |
| `...a roll posted by an interrupt in the middle of a drain goes in after the rest` | clean-up | Passed without new code. A fake interrupt handler |
| `...the thread test wraps and fills the 21-roll mailbox again` | clean-up | The 21-roll mailbox had silently weakened it |
| `...refuse to build where a count would need a lock` | clean-up | From the review |
| `...two readers can each hear about every lost roll` | make-change | After review: `Pinsetter_RollsLost` was a query that changed state |
| `...a drain takes only the rolls waiting when it starts` | make-change | After review: a bound on each drain |
| `...refuse a drain from inside a listener` | make-change | After review. `Game_IsNotifying`, in a new private header, `game_internal.h` |
| `...accept a NULL pinsetter, as every Game_* function accepts a NULL game` | make-change | After review. Reversed three commits later |
| `pinsetter.h: two contracts the caller keeps, a glitch's slot and detaching before destroy` | clean-up | After review. Comments only |
| `Revert "Pinsetter: accept a NULL pinsetter, ..."` | clean-up | The user reversed the decision |
| `...stop the program when a pinsetter can't be created, in every build` | make-change | `Fault_Stop`, in a new public header, `fault.h`, with the host version in `src/fault.c` |
| `Refactor: GAME_ERR_EDIT_DURING_NOTIFICATION becomes GAME_ERR_DURING_NOTIFICATION` | clean-up | One refusal for a roll, an edit and a drain |
| `Remove the listener mailbox: a roll from inside a listener is refused again` | clean-up | The user's decision. `GAME_QUEUED` is gone |
| `Refactor: the pinsetter no longer asks whether the game is notifying` | clean-up | `Game_IsNotifying` and `game_internal.h` are gone |
| `Pinsetter: stop the program when two posts overlap, in a debug build` | make-change | The user's design. `pinsetter_hooks.h`, and a white-box death test |

From the remote scoreboard on, every commit message names a mutation that fails the commit's
test. In every case but one (the remote scoreboard, below), it fails only that test.

### One boundary, crossed both ways

The pinsetter and the remote scoreboard are one boundary, seen from its two sides. Rolls come
in from a machine the game doesn't own, and scores go out to a display it doesn't own.
Neither side holds a pointer into the game:
- The interrupt side has no `Game` at all. `Pinsetter_Post` takes a pinsetter and the pins,
  nothing else. Only the main loop, which passes a game to `Pinsetter_Drain`, joins the two.
- The remote scoreboard gets 4 bytes for each message (the frame number, the score low byte
  then high, and whether the frame is complete). It rebuilds the scoreboard from those bytes
  alone, through a correction that reopens a frame.

**The prediction held: the remote scoreboard needed no change to `game.h` or `game.c`.** The
frame-changed message was already made of values only (a number, a score and a flag) since
phase 5 made it describe a frame's state rather than an event. A message made of values can
be put on a wire as it is. The test's mutation shows the same thing from the other side.
Stopping the reopened-frame message fails the remote scoreboard, but also the five
correction tests that already check that message. No mutation fails the remote scoreboard
alone, because a listener at the end of a wire needs nothing a local listener doesn't
already get. **Phase 5's choice to send states, not events, is what made the far side
possible**, and it cost nothing now.

The incoming direction is where the work was. Going out, the game calls a listener
synchronously. Coming in, a roll arrives on another thread, so a synchronous call won't do:
the main loop may be in the middle of a notification, an edit, anything. The mailbox makes
the incoming direction asynchronous. The sender posts and doesn't wait, and the roll is
delivered later, when the receiver is ready.

### Two mailboxes, two jobs

Phase 6 had two mailboxes. They look alike, but they aren't. (The game's was removed after
review, for the reasons in this table: see "After review, again".)

| | The game's mailbox | The pinsetter's mailbox |
|---|---|---|
| Why it exists | A listener rolled while the game was telling the listeners: re-entry | A roll arrived on another thread: concurrency |
| Threads | One | Two, each writing only its own position |
| Shape | A `RollLog`, shifted on each take | A ring of 22 slots, with two atomic positions |
| A roll it can't apply | Dropped; the rest apply (`d860bc8`) | Kept; draining stops there (the user's policy) |
| Who hears about it | Nobody: the sender was told `GAME_QUEUED` and has moved on | The main loop, from `Pinsetter_Drain`'s status |

They share no code, and sharing it would be wrong: the game's needs no atomics, and the
pinsetter's can't shift its contents under the other thread.

### The user's decisions

**A roll the machine reported that the game rejects.** The pinsetter used to drop it
silently, with `(void)Game_Roll(...)` in `Pinsetter_Drain`. (The constitution's `ENG-3.7`
says never to discard a status silently.) The options put to the user were: drop it
silently; drop it and report it; or stop and keep it until someone resolves it. The deciding
fact is a bowling one. An "impossible" roll is often the right one, made to look impossible
by an earlier miscount: the machine recorded 5 when 2 fell, and the true 8 then looks
impossible. Dropping it loses the one roll that was right. **The user chose to stop and keep
it**, and to size the mailbox to a whole game, 21 rolls, so that a stopped drain can never
cost the interrupt handler a roll of the game.
- The scorer resolves a stopped drain in one of two ways, both tested. Either correct the
  earlier roll with `Game_CorrectRoll`, which already existed, and drain again. Or discard
  the reported roll as a glitch, with `Pinsetter_DiscardOldest`, which the second test
  pulled in.
- One consequence passed without new code. Rolls reported after the tenth frame (the next
  bowler starting before the main loop moves on) are refused with `GAME_ERR_GAME_OVER`, and
  wait for the next game. Before, they vanished.
- One consequence for the interrupt side: a slot now goes back to it only after the game has
  taken the roll in it, because until then the roll may have to stay.
- **Not decided: the game's own mailbox still drops** an impossible roll that a listener
  queued (`d860bc8`). The question was asked about the pinsetter. A listener's roll comes
  from software, not the machine, and its sender was told `GAME_QUEUED` and can't be told
  again. This one is left to the user.

**A full mailbox.** The interrupt handler's roll was lost, and only the handler saw `false`,
with no one to tell. **The user chose a count.** The interrupt side increments it and the
main loop never clears it. At first `Pinsetter_RollsLost` answered the difference from the
last value the main loop saw; after review it returns the total, and each reader takes its
own difference (see "After review"). Unsigned wrap-around keeps the difference right. The
count is 16 bits, so a test can reach the wrap (65,536 lost rolls), and one does. With a
21-roll mailbox, any increase is a real anomaly, and `pinsetter.h` says so.

### A firmware review of the mailbox

This is the review the constitution's C / MISRA-C avatar would make, against its Engineering
laws on architecture, code quality, resiliency and AI review. The other three avatars (ISO
26262, and the two product lines) don't apply to a bowling scorer, and no safety claim is
made. It is an AI review, so by `ENG-9.2` it is a first pass, not an approval. Each finding
is marked:
- **tested:** a deterministic test fails without it;
- **checked:** ThreadSanitizer or the compiler rejects the broken version;
- **argued:** reasoning only.

| Property | Finding | Status |
|---|---|---|
| **Memory ordering** | Each side publishes its own position with a release store, after touching the roll, and reads the other side's with an acquire load, before touching one. No `volatile`: the positions are C11 atomics | **Checked.** ThreadSanitizer catches each of the four orderings weakened to relaxed, one at a time (the table below). It also catches the plain version with no atomics at all, which confirms what `0107421` could only claim |
| ...on a real target | ThreadSanitizer checks the C11 model on an x86 host, with two threads. An MCU's interrupt handler preempts the main loop on one core, and the C11 guarantee that carries over to that is for lock-free atomics | **Argued**, and **checked** that both atomics are lock-free (below) |
| **One producer, one consumer** | Documented at the top of `pinsetter.h`: only the interrupt side may post, and a listener that wants to roll calls `Game_Roll`. It isn't repeated on `Pinsetter_Post` itself | Documented; nothing checks it (next row) |
| ...if a second producer posts | Two posts can read the same position, write the same slot and both return `true`: a roll is lost, or read twice. In the probe, the plain builds lost and repeated rolls (14, 21 and 10 frames where there should have been 20). **ThreadSanitizer never saw it**, even at 20,000 runs and 800,000 posts. After review, a debug build stops the program when two posts overlap, and the rule is stated on `Pinsetter_Post` | **Checked in debug, when posts overlap: a net, not a proof.** Not caught by ThreadSanitizer |
| **Interrupt safety** | `Pinsetter_Post` does two atomic loads, one plain store and one atomic store; when the mailbox is full, one load and one store. No loop, no callback, no lock, and no game, because it has no `Game` to reach. The lost-roll count is written with a load and a store, not a read-modify-write: it has one writer, and some interrupt-driven targets can't do an atomic read-modify-write without a lock | Bounded, with no callback: **argued**, by reading (and no `Game` is in reach, by its signature). No lock: **checked** by two static asserts that both atomics are lock-free. Posting never touches the game: **tested** (a fake interrupt handler, fired in the middle of a drain, sees the score unchanged) |
| **A full mailbox** | Was invisible to all but the interrupt handler. Now counted, and read by the main loop. After review, reading it changes nothing, so any number of readers can watch it | **Tested**: the count, its wrap, and two readers. A read that writes no longer compiles (the query takes a `const Pinsetter *`) |
| **Wrap-around** | The positions stay below 22 and wrap to 0, so no counter can overflow. The earlier free-running counts, taken `% 8`, were right only because 8 divides 2^32. Taken `% 21`, they would have skipped slots after 2^32 posts | The positions' wrap: **tested** (the thread test sends 105 rolls through 22 slots, and a ring that never wraps crashes it). The 2^32 case: **argued**, and designed out |
| **Capacity at its edges** | 21 rolls accepted, the 22nd refused, and room again after a drain | **Tested**, one off each way: a 22-roll mailbox fails the test, and a 20-roll one fails the build (the static assert tying it to `GAME_MAX_ROLLS`) |
| **The 21-roll promise has an edge** | It holds while every waiting roll is a real roll of one game. A glitch waiting to be discarded takes a slot too, and so would a roll of the game before, still unresolved. Then 21 more rolls make 22, and the last is refused (and counted) | **Argued**. After review, the user accepted it: documented in `pinsetter.h`, not sized for, because the count makes it visible |
| **A drain had no fixed bound** (`ENG-7.4`, bounded waits) | `Pinsetter_Drain` read the post position again on each pass, so it also took rolls that arrived while it ran. An interrupt handler posting faster than the game applies rolls could keep it draining. After review it reads the position once, at the start: a drain applies at most 21 rolls, and later ones wait for the next | **Tested** (the fake-interrupt test now expects the next drain, and fails if the position is read on every pass) |
| **Draining from inside a listener** | `Game_Roll` returned `GAME_QUEUED`, which the drain treated as accepted, and the roll went into the game's mailbox, where an impossible one is dropped: around the user's stop-and-keep policy. After review the drain refused it itself; after the listener mailbox was removed, the game refuses the roll (`GAME_ERR_DURING_NOTIFICATION`), and the drain keeps it, as it keeps any refused roll | **Tested** (a glitch waiting in the pinsetter vanished before; it is still there after) |
| **`NULL`, and lifetime** | The `Pinsetter_*` functions crashed on `NULL`, unlike every `Game_*` function. `NULL` could only come from `Pinsetter_Create` running out of its pool. After review, `Pinsetter_Create` stops the program instead, in every build, and "never `NULL`" is a documented precondition of the rest. Destroying a pinsetter while its interrupt handler can still post lands the post in the next pinsetter created: the pool can't see that, so `pinsetter.h` makes it the caller's contract | Creation stopping: **tested**, in every build. Never `NULL` after that: **argued**, a precondition. Lifetime: **argued**, and documented |

**ThreadSanitizer, one mutant at a time.** Each mutant ran alone, in its own commit and its
own CI run, on a throwaway branch that has since been deleted. The thread test runs its five
games 20 times per run; for the second producer, the stress was raised until something
failed. The key lines of each report are copied here because Actions logs expire. The line
numbers are those of `src/pinsetter.c` as committed: line 92 is the post writing the roll,
and line 101 the drain reading it.

| Mutant | CI run | Caught? | ThreadSanitizer's report |
|---|---|---|---|
| None (the baseline) | [36290752684](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36290752684) | All four builds pass | None |
| M0: plain positions, no atomics | [36290833004](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36290833004) | Yes, at 20 runs | Three races. `Write of size 4 ... by main thread: Pinsetter_Drain pinsetter.c:107` / `Previous read of size 4 ... by thread T1: Pinsetter_Post pinsetter.c:83` (the drain position). `Read of size 1 ... Pinsetter_Drain :101` / `Previous write of size 1 ... by thread T1: Pinsetter_Post :92` (the roll). `Read of size 4 ... Pinsetter_Drain :100` / `Previous write of size 4 ... by thread T3` (the post position) |
| M1: the post publishes with a relaxed store | [36290906278](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36290906278) | Yes, at 20 | `Read of size 1 ... by main thread: Pinsetter_Drain pinsetter.c:101` / `Previous write of size 1 ... by thread T1: Pinsetter_Post pinsetter.c:92` / `Location is global 's_pinsetters' of size 72` |
| M2: the drain reads the post position relaxed | [36290947961](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36290947961) | Yes, at 20 | The same pair: the drain's read at :101 after the post's write at :92 |
| M3: the drain gives a slot back with a relaxed store | [36290979034](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36290979034) | Yes, at 20 | `Write of size 1 ... by thread T1: Pinsetter_Post pinsetter.c:92` / `Previous read of size 1 ... by main thread: Pinsetter_Drain pinsetter.c:101`: the interrupt side reusing a slot the main loop had read |
| M4: the post reads the drain position relaxed | [36291015323](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36291015323) | Yes, at 20 | The same pair as M3 |
| M5: a second producer (two threads post 20 gutter balls each) | [20 runs](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36291053588), [200](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36291103816), [2,000](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36291140274), [20,000](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36291186957) | **No, at any stress.** The plain builds caught lost and repeated rolls from 2,000 runs (debug first at run 259, release at run 1,278), and at 20,000 (in 14 runs in debug, 10 in release and 6 under UBSan) | None |

Two results the review didn't expect:
- **M3 and M4 need the ring to wrap.** They are races on *reusing* a slot. The thread test as
  it stood before the review (one game of 21 rolls into a fresh 22-slot ring) never wrapped,
  so it could not have caught either. That is the weakened test described under Costs.
- **ThreadSanitizer is not the tool for a second producer.** It reports two accesses that
  have no ordering between them, and most of the time here there is one: between two posts,
  the consumer's acquire and release link the two producers. Only two posts that read the same
  position at the same moment would show, and under ThreadSanitizer's slowdown that never
  happened. The score check in the uninstrumented builds, which run fast enough to collide,
  caught it. So nothing checks the one-producer rule while the program runs.

### Watched: did the mailbox push validation into the frames?

**No. `Game` still checks every roll at the door.** A queued roll, and a drained one, goes
through the same `Game_Accept` as any other: game over, more pins than are standing, and
the rule's range. Neither mailbox validates anything. They carry pins, and pass on the game's
answer. What did appear is tell-don't-ask *at the boundary*. The pinsetter doesn't ask the
game whether a roll would be accepted: it tells the game the roll, and acts on the answer
(stop and keep). Inside the game, `Game` still asks the latest frame how many pins are
standing. Phase 2's reason for declining still holds: no test needs a frame to validate its
own roll.

### Watched: are the listeners and the mailbox one module or two?

**The listeners and the game's mailbox are one module.** They share one flag, `notifying`:
it is what sends a roll to the mailbox, and it is set only while the listeners are being told.
The mailbox exists only because a listener can roll, it is emptied only after a notification
ends, and the loop in `Game_Roll` that empties it is the tail end of telling the listeners.
Take the listeners out of `game.c`, and the mailbox has to go with them. **The pinsetter is a
second module, and already apart**, in its own files, sharing nothing with the game but
`GAME_MAX_ROLLS`. So the clean-up has three pieces, not four: the roll log with its replay,
the listeners with their mailbox, and the pinsetter as it is. Not started: the user's call.

*After review:* the game's mailbox is gone, so the question answered itself. What is left in
`game.c` is the roll log with its replay, and the listeners with their one flag. The pinsetter
stays apart.

### Kay's three properties

| Property | Moved? | Evidence |
|---|---|---|
| **Messaging** | Yes, the most yet | For the first time a message is **asynchronous**: the interrupt side posts and returns without waiting, and the roll is delivered later, when the main loop is ready. The sender has no reference to the receiver at all. Going out, a message is now **data that crosses a wire**: 4 bytes a frame, rebuilt by a receiver that shares no code with the game. And a message the receiver can't accept is no longer lost: it waits for a person |
| **Hiding of state-process** | Yes | The interrupt side can't reach the game: `Pinsetter_Post` has no `Game` to pass. The ring and its positions are private to `pinsetter.c`. The far side of the wire knows a byte format, not a type |
| **Late binding** | A little | Which game gets a roll is decided when it is delivered, not when it is sent. The main loop passes a game to each drain, and a roll reported after the tenth frame goes into whichever game comes next. It is a parameter, not dispatch |

**Cook.** The pinsetter is an abstract data type: one implementation, behind an opaque
handle. The remote scoreboard is the first receiver that shares nothing with the library but
a protocol. That is closer to Cook's objects, which work together only through an interface,
than anything before. But the interface is a byte format, the way two processes talk, not two
objects.

### Costs

- **Size:** `src/` grew from 1,098 lines to 1,268 (762 to 882 of code), and `include/` from
  128 to 199 (40 to 61). Most of it is the pinsetter: `pinsetter.c` is 130 lines and
  `pinsetter.h` 65. The rest is the game's mailbox, the new statuses, and their comments.
  The review's follow-up added 37 more lines to `src/` (23 of code: the `NULL` guards, the
  private header and the drain's guard) and 21 lines of comments to `include/`.
- **Memory, checked with `offsetof` and `sizeof`:**
  - A `Game` grew by 24 bytes, from 1,040 to 1,064: the mailbox, another 22-byte `RollLog`,
    at offset 1,040, then 2 bytes of padding. The pool of two games is 2,128 bytes.
  - A `Pinsetter` is 36 bytes: 22 roll slots, 2 of padding, two 4-byte positions, the 2-byte
    lost count and the 2-byte last value seen. The pool of two is 72 bytes, the same 72 that
    ThreadSanitizer reports as the size of `s_pinsetters`. It was 16 bytes before the review:
    the whole-game mailbox added 14 slots and 2 of padding, and the lost count 4. After
    review the last value seen is gone, and a `Pinsetter` is still 36 bytes: 4-byte alignment
    turned those 2 bytes into padding.
- **What the compiler and the sanitizers can't check:**
  - **One producer.** ThreadSanitizer can't see a second producer in practice (above), and
    nothing else looks.
  - **The memory ordering on a real target.** ThreadSanitizer checks the C11 model on a host.
    The target's compiler and instruction set are trusted to implement it for lock-free
    atomics. The static asserts check the lock-free part, on each compiler the build runs on.
  - **That the interrupt side stays bounded.** Nothing stops a later change adding a loop or
    a call to `Pinsetter_Post`. Only review does.
  - **A pinsetter's lifetime.** Detaching the interrupt handler before `Pinsetter_Destroy`
    is the caller's contract, in `pinsetter.h`; nothing checks it. (The drain's bound and a
    drain from inside a listener were on this list until the review's follow-up made each
    one tested.)
  - **Never `NULL`.** A precondition of every function but `Pinsetter_Create`: nothing checks
    it, on purpose, because the only way to get `NULL` now stops the program first.
  - **That a target's `Fault_Stop` never returns.** `_Noreturn` is a promise the compiler
    trusts, not one it checks: one that returned would carry on with no pinsetter.
- **A test was weakened silently.** Making the mailbox 21 rolls left the thread test posting
  one game into a 22-slot ring. It never wrapped or filled again, though its comment said it
  did, and every build stayed green. Only reasoning found it, and a mutant (a ring that never
  wraps) confirmed it. The test now sends five games through one pinsetter, and a probe
  counted the mailbox full in 20 of 20 runs.

### The strongest argument that this still isn't Kay OO

The asynchrony came from the hardware, not from the design. The "message" is one byte in a
ring buffer: it has no selector, carries no reply address, and there is only one kind. The
receiver can't decline it. It can only fail to apply it, and that answer goes to the main
loop, not to the sender, which can never be told. The mailbox is the standard queue of any
interrupt-driven firmware, with one producer and one consumer, and it was well understood
long before objects. Erlang's processes have mailboxes too, but they receive many kinds of
message, matched as they arrive, with a process behind each mailbox. Here there is one queue,
one consumer and one kind of message. And the wire going out carries a hand-rolled, fixed
4-byte format. What makes the far side possible is that the message is plain data: the
opposite of an object hiding its state.

### Would a simpler design do?

- **For the pinsetter: not much simpler, in portable C.** On a single-core MCU, a plain queue
  with interrupts turned off around the main loop's side is simpler to reason about, and
  needs no atomics. But turning interrupts off is a platform call, a port of its own in the
  constitution's terms (`ENG-2.2`), and a host test with a real thread can't use it. The
  lock-free ring is the simplest thing that runs, and is checked, on both. Its one real
  extra, the spare slot that tells full from empty, costs a byte.
- **For the game's mailbox: yes, the refusal was simpler.** Phase 3 refused a roll from
  inside a listener with one flag. The queue is the brief's own feature, not a need the
  pinsetter created: the pinsetter's rolls never go through it, because the main loop drains
  outside any notification. Of everything in phase 6, the game's mailbox buys the least.
  After review it was removed (below).
- **For the policy: dropping was simpler, and wrong.** Stop-and-keep cost one status, one
  function (`Pinsetter_DiscardOldest`) and 14 more slots. Dropping loses the roll that was
  right.

### Where the tests overturned my reasoning

- **An open frame scores 0**, not the pins so far. I got that wrong twice, in the first
  drafts of two tests (the stopped drain, and the fake interrupt), and the tests said so.
- **I expected ThreadSanitizer to catch a second producer**, and said so when asking the user
  how to run the probes. It didn't, at any stress. The plain builds' score check did.
- **The thread test had stopped testing the wrap**, after my own change to the capacity, and I
  didn't notice until I was reasoning about M3. The never-wrap mutant confirmed it.
- **The naive capacity change passed every test.** Here the tests didn't disagree with the
  reasoning; they were silent, because 2^32 posts is out of their reach. So that step is
  argued, and the design removes the case rather than a test covering it.
- One claim was confirmed instead. `0107421`'s "only ThreadSanitizer can see the race" in the
  plain version had never been run. M0 now shows the three races.

### After review: one smell, and the four items left to the user

**A query that changed state.** `Pinsetter_RollsLost` updated the pinsetter's `lost_seen` as
it answered, so two readers, a scoreboard and a logger, would each steal the other's
increments. A test with two readers showed it: the logger heard 0 after the scoreboard had
asked. Following command-query separation, it now returns the total, never cleared, and each
reader keeps its own last value and takes its own difference. The `lost_seen` field is gone,
and the query takes a `const Pinsetter *`, so a version that writes no longer compiles. (GCC
accepts `atomic_load_explicit` on a `const` object; C11's own wording doesn't promise it.)
The earlier version was mine, and the review caught it, not a test: no test had two readers.

The four items the review left to the user, each now settled:

| Item | The user's choice | What changed | Evidence |
|---|---|---|---|
| A drain with no fixed bound | Bound it: a firmware main loop needs bounded work per pass (`ENG-7.4`) | `Pinsetter_Drain` reads the post position once, at the start. A roll posted mid-drain goes in on the next drain. Its own commit, because it is a policy change | The fake-interrupt test changed to match. Reading the position on every pass again fails it, and only it |
| Draining from inside a listener | Refuse it, with the status edits get | `Pinsetter_Drain` returned `GAME_ERR_EDIT_DURING_NOTIFICATION` there, asking the game first through `Game_IsNotifying`, in a private header. Both went again once the game refused rolls itself (below) | A glitch waiting in the pinsetter vanished before the guard, and is still there after. Removing the guard fails that test, and only it |
| A glitch in one of the 21 slots | Accept it and document it | A paragraph on `Pinsetter_Post` in `pinsetter.h` | Argued; the lost-roll count makes it visible |
| `NULL` and lifetime | First: accept `NULL` as every `Game_*` function does. Then reversed (below): stop at creation. Lifetime is the caller's contract | `Pinsetter_Create` calls `Fault_Stop` when its pool is used up; the other functions take a pinsetter as a precondition. One line on `Pinsetter_Destroy`: detach the interrupt handler first | A death test uses up the pool, in every build. Removing the check fails it in all three builds; making it an assert fails it in release only |

**A decision reversed: guards for `NULL`, then a stop at creation.** The first answer to
`NULL` was consistency: accept it quietly, as every `Game_*` function does. It was committed
and pushed, and the user then reversed it, before anything else was built on it. The
reasoning:
- `NULL` reaches a pinsetter function only when `Pinsetter_Create` runs out of its pool. A
  game can reasonably run out at runtime, when a league runs more games than the pool holds,
  so `GAME_ERR_NULL_GAME` stays. A lane's pinsetter can't: the lanes are fixed when the system
  is built, so running out is a configuration error.
- Quiet guards put that error where it hurts. An interrupt handler posting into a pinsetter
  that was never created lost every roll, with no one told: the very failure this phase made
  visible. And every post paid a comparison for it.
- A **Null Object** would do the same, more elegantly: a static pinsetter whose every call
  does nothing harmful. The code already has a good one, the standard rule
  `Game_CountPinsDown`, which models "no variant" as a rule that changes nothing, so
  `Game_Accept` has no special case. But a null pinsetter would lose rolls exactly as silently,
  and would add an indirect call to the interrupt path.
- So creation stops the program, at the moment the mistake is made. Not with `assert`, which
  a release build removes, leaving no safe value to return: with `Fault_Stop`, a fail-stop
  that stays in every build. `fault.h` declares it. The host version writes the reason and
  calls `abort()`; a target replaces it at link time by defining its own. A probe checked
  that: a program with its own `Fault_Stop`, linked against `libbowling.a`, ran its own, with
  no duplicate symbol.

The quiet guards were my recommendation, reached for consistency; the reversal is the user's.
In the log, three commits stand for it: the guards, their revert, and the stop. The first
two cancel out.

Two small things these left behind, both resolved by the next round:
- `GAME_ERR_EDIT_DURING_NOTIFICATION` also answered a drain, which isn't an edit. It is now
  `GAME_ERR_DURING_NOTIFICATION`.
- `game_internal.h` was the first header through which a library module asked `Game` about
  its state: asking, not telling. It is gone.

### After review, again: the listener queue taken out, and a net for one producer

**The listener queue, added on principle and taken back out by the method.** At the start of
phase 6, a roll from inside a listener stopped being refused and went into a queue in the
game. That came from a reviewer's suggestion, not from a feature: no test for a real
behavior needed a listener to roll. The rest of the phase showed what it cost and how little
it bought:
- The pinsetter never used it. The main loop drains outside any notification.
- It dropped a queued roll that turned out impossible, silently, to a sender already told
  `GAME_QUEUED`.
- It was the way around the stop-and-keep policy, and closing that needed a guard in the
  pinsetter and a private header through which the pinsetter asked the game about its state.

The user decided to remove it, as clean-up. In three commits:
1. The status becomes `GAME_ERR_DURING_NOTIFICATION`.
2. The queue goes, and a roll from inside a listener is refused again, as in phase 3: one
   refusal for a roll, an edit and a drain. `GAME_QUEUED` goes from the end of the enum, so
   no other status changes number. The two tests of the queue go with it.
3. The pinsetter's own guard, `Game_IsNotifying` and `game_internal.h` go. A drain from
   inside a listener now just gets the game's refusal for its first roll, and keeps that roll,
   as it keeps any refused roll. The pinsetter tells the game and acts on the answer, instead
   of asking first. One edge moved: with nothing waiting, such a drain returns `GAME_OK`.

The result is the clearest evidence yet for this log's conclusion. **`game.c` is back to 344
lines, and a `Game` back to 1,040 bytes, exactly as before phase 6.** Against its version
before the phase, `game.c` differs by 10 lines: the renamed status, `GAME_MAX_ROLLS` moved to
a shared header, and comments. Everything phase 6 kept is the pinsetter, a new module at the
boundary. The one thing phase 6 added inside the game, it took back out.

**A net for one producer.** The one-producer rule was enforced only by review, and
ThreadSanitizer never caught a second producer. The user chose neither option offered (a
debug-only owner check needing a port, or a ring safe for many producers) but a simpler one:
detect overlapping posts, which is the failure itself, not who is posting.
- `Pinsetter_Post` test-and-sets an `atomic_flag` on the way in, and clears it on the one way
  out, through a wrapper, so no early return can leave it set. If the flag was already set,
  it calls `Fault_Stop`. `atomic_flag` is the one atomic type C11 promises is lock-free.
- It needs no port, and it also catches a nested interrupt posting on one core, which an
  owner check wouldn't.
- It compiles in unless `NDEBUG`, and a target may set `PINSETTER_CHECK_OVERLAP=0`. The flag
  fits in existing padding: a `Pinsetter` is 36 bytes either way.
- The rule is now stated on `Pinsetter_Post` itself, not only at the top of the header.
- Test: a white-box death test, in its own file, with a hook that leaves a post under way.
  Removing the stop fails it, and only it. A flag never cleared stops every second post, and
  fails 12 tests.

**A net, not a proof.** A probe ran two real producer threads against the library on this
machine (8 cores, Windows). Without the check, no roll was lost in 60,000 runs: here, the
two producers' posts almost never overlap. With the check, it caught one overlap in about
146,000 runs, and that overlap hadn't lost a roll. So the check sees overlaps a score check
can't, but only when one happens. Recorded as **checked in debug, when posts overlap**.

The clean-up followed, with the user's go: see "The clean-up deferred from phase 6".

### The debt

Before phase 6, `game.c` was 344 lines, 44% of the library's `.c` files (773 lines), with the
roll-log and listener extractions deferred on purpose. At the end of phase 6 it was **375
lines, 40% of 934**; after the review's first follow-up, 381 of 956, still 40%. **After the
listener queue was removed, it is 344 again: 36% of 952.**
The share fell only because `pinsetter.c` (130 lines) joined the library. `game.c` itself grew
by 31 lines: 33 for the game's mailbox, less the 2 that `GAME_MAX_ROLLS` took with it when it
moved to `game_limits.h`. Counted together, the two boundary files, `game.c`
and `pinsetter.c`, were 505 lines, 54% of the library at the end of phase 6. Now they are
510 (344 and 166), 54% of 952: the pinsetter grew by its checks and contracts, and the game
shrank back to where it started. The boundary is still more than half of the code, and the
frames, which are the whole design on `main`, are the smaller part. (After the clean-up that
followed, `game.c` is 280 lines: see its own debt section.)

---

## The clean-up deferred from phase 6

**The brief.** A refactoring, with no change in behavior: improve the design against SOLID,
GRASP, measured coupling and cohesion, and the constitution's Embedded C smells (with Fowler's
catalogue only where those don't reach). The tests don't change; all four builds stay green
after every commit; the interrupt path gets no new calls, stack or indirection; and where a
principle and simplicity disagree, simplicity wins and the disagreement is recorded here.
Every commit was pushed on its own, so CI, ThreadSanitizer included, ran on each.

### What was done

| Commit | Serves | Smell (constitution's name and law) |
|---|---|---|
| `Frame_IsComplete: the context and RegularFrame ask the base, not its field` | Information Expert, encapsulation | Feature Envy / Deep struct navigation (`ENG-3.3`) |
| `Frame_InitSpare: one place says how a spare is built, for both families` | Information Expert, High Cohesion | Duplicate code (`ENG-3.9`) |
| `Frame_InitStrike: one place says how a strike is built, for both families` | Information Expert, High Cohesion | Duplicate code (`ENG-3.9`) |
| `Game_ReportFrames: telling after a roll is telling after an edit, from the first unreported frame` | High Cohesion | Duplicate code (`ENG-3.9`): the same concept, restated |
| `Extract the listeners from game.c into FrameListeners` | Single responsibility, High Cohesion, Pure Fabrication | Resource-budget drift (`ENG-1.3`), met with `static inline` |
| `Extract the roll log from game.c into RollLog, with the edit as a RollEdit` | Single responsibility, Information Expert | Data clump (`ENG-3.9`) |
| `RollNumber and FrameNumber: the numbers that count from 1 get names` | Protected Variations, intent | Primitive obsession (`ENG-3.7`) |
| `Game_ReportFrames inline: less stack under every listener callback` | Low Coupling's cost, kept in budget | Resource-budget drift (`ENG-1.3`) |

No Fowler smell was needed: each finding had a name in the embedded catalogue.

### Measured: the modules, before and after

Lines in the file (code lines, without comments or blanks), and the project headers each
includes. Only the files that changed are listed.

| File | Before | After | Includes, after |
|---|---|---|---|
| `src/game.c` | 344 (269) | **280 (216)** | `game.h`, `frame_context.h`, `frame_listeners.h`, `roll_log.h`, `slot_pool.h` (was `game_limits.h` in place of the two new ones) |
| `src/frame_listeners.h/.c` | none | 58 + 21 (33 + 18) | `bowling_types.h`, `game.h` |
| `src/roll_log.h/.c` | none | 50 + 52 (22 + 45) | `bowling_types.h`, `game.h`, `game_limits.h` |
| `src/frame.c` | 94 (76) | 115 (94) | unchanged |
| `src/frame.h` | 119 (49) | 128 (52) | unchanged |
| `src/spare_frame.c` | 24 (19) | 21 (16) | unchanged |
| `src/strike_frame.c` | 23 (20) | 21 (18) | unchanged |
| `src/tenth_frame.c` | 64 (51) | 59 (46) | unchanged |
| `include/bowling_types.h` | 20 (6) | 27 (8) | none |
| all of `src/*.c` | 952 | 972 | |

The library grew by 20 lines of `.c` in all: two small modules, each with its own boilerplate,
in exchange for two reasons to change leaving `game.c`.

### Measured: which of `Game`'s fields each function touches

Before, three groups of fields, and two functions that needed all three:

| Group | Fields | Functions touching only it |
|---|---|---|
| The frames | `frames`, `frame_count`, `count_pins` | `IsOver`, `HasNoFrames`, `PinsStanding`, `AddNewFrame`, `ApplyPinsToFrames`, `Score`, `Accept` |
| The listeners | `listeners`, `listener_count`, `notifying` | `TellListeners`, `OnFrameChanged` |
| The log | `log` | (none alone: `Roll` and `EditRolls` also set or read `notifying`) |
| Spanning | `frames_reported` with the frames and the listeners | `ReportCompletedFrames`, `ReportCorrection` (both) |

After, the listener group is one field (`listeners`, a `FrameListeners`) and the log is one
(`log`, a `RollLog`), each reached only through its module's functions. What is left in
`game.c` touches:

| Function | Fields |
|---|---|
| `IsOver`, `PinsStanding`, `AddNewFrame`, `ApplyPinsToFrames`, `Score` | `frames`, `frame_count` |
| `Accept` | `count_pins` (and the frames through the functions above) |
| `ReportFrames`, the one bridge | `frames`, `frame_count`, `frames_reported`, `listeners` |
| `Roll` | `listeners`, `log`, `frames_reported` |
| `Replay`, `ApplyEditedLog`, `EditRolls` | `log` (and `frame_count`, `frames_reported` or `listeners` for their one step each) |
| `CreateWithRule` | every field, once, to set it up |

No function in `game.c` reaches into a listener entry or a logged roll any more.

### Measured: stack and memory (`ENG-1.3`)

Release build, `-fstack-usage`, on a 64-bit host. Chains add each function's frame to its
callee's:

| Chain | Before | After |
|---|---|---|
| `Game_Roll` to a listener callback | 80 | 128 |
| `Game_CorrectRoll` to a listener callback | 288 | 224 |
| `Game_Roll` to `Game_Accept` | 160 | 208 |
| `Game_CorrectRoll` to `Game_Accept` (the deepest) | 368 | 304 |
| `Pinsetter_Post`, the interrupt path | 8, no calls | 8, no calls: untouched |

The deepest chain fell by 64 bytes, and so did the base under a callback after an edit. The
base under a callback after a roll grew by 48, because the report walk now has two callers and
is bigger when inlined into `Game_Roll`.

**A correction.** My step-by-step stack notes missed that, for four commits. Merging the two
report walks left GCC keeping the merged function out of line, so a callback after a roll ran
on 176 bytes, not 80. The listener extraction's commit then said the stack was "back to exactly
what it was", which was true only against its parent. Measuring the whole clean-up against its
start found it, and one more commit (`Game_ReportFrames` inline) brought it down to the 128
above. The lesson for a resource budget: compare with where you started, not with the last
step.

**Memory:** a `Game` grew from 1,040 to 1,048 bytes (the pool of two, 2,080 to 2,096). The
listeners' count and flag used to sit in `Game`'s existing padding; inside their own struct they
pad it to 40 bytes. That is the price of the listener module, paid once per game. On a 32-bit
target, with 4-byte pointers, it should be less, but that is argued, not measured.

### One module or two

- **The listeners are one module, with their re-entry flag.** Phase 6 asked whether the
  listeners and the game's mailbox were one module or two. The mailbox is gone, so what is left
  is the listeners and the flag that refuses a change from inside one, and they are one module:
  the flag is set only by telling the listeners, and read only to refuse a change.
- **The roll log is one module, with its edit.** Whether a range is rolls the log has, and
  whether the edited log still fits, are questions about its own count, so the checks and the
  splice moved with it. Replaying the log did not: it needs `Game_Accept` and the frames, so it
  stays in `game.c`.
- **`frames_reported` belongs to neither.** It is "how many frames the listeners have been told
  are complete": a fact about the frames and the listeners together. It stays in `Game`, the one
  place that sees both, with `Game_ReportFrames`, the only function that uses both.

### Considered and not done

| Candidate | Why not |
|---|---|
| `SlotPool_Make`, dead code (`ENG-3.6`) | Its only callers are `slot_pool_test.cpp`. Removing it means changing that test, which isn't a test moving with its code |
| `Game_EditRolls`'s five parameters, Long Parameter List (`ENG-3.1`) | Not in the clean-up, because changing the public signature changes the tests. **Decided after it, by the user:** `RollEdit` became public, in `game.h`, and the API is `Game_EditRolls(Game *game, const RollEdit *edit)`: two parameters, the edit by `const` pointer, with no copy. An API change, with no change in behavior; the tests build their edits with a small `MakeEdit` helper, because C++17 has no designated initializers for a C struct |
| Splitting `game.h` by client (interface segregation) | No client benefits. The pinsetter uses `Game`, `GameStatus` and `Game_Roll`; the two new modules use `GameStatus` or `FrameChangedCallback`; the tests use everything, through one `test_support.h`, which would have to change. The `frame_context.h` / `frame_transition.h` split on `main` hid calls from a client that must not make them; nothing here is like that |
| Dependency inversion for the pinsetter (a roll sink in place of `Game_Roll`) | One sink exists, and none is planned: Speculative generality (`PRD-1.1`), and an indirect call on the drain path |
| The frames group as its own module | It is what a `Game` *is*: the frames and the rule that feeds them. Taking it out would leave `Game` forwarding every call, the constitution's Ravioli (`ENG-3.9`). Simplicity wins; recorded |
| The two pool lookups (`Game_FindSlot`, `Pinsetter_Destroy`) | Duplicate code (`ENG-3.9`), but a shared version needs `void *` and an element size, or pointer subtraction: types lost, and MISRA's pointer-arithmetic rules in play, for two five-line loops |
| Moving roll validation into the frames (tell, don't ask) | Declined in phase 2, and the clean-up didn't make it more natural: `Game_Accept` still asks the latest frame how many pins are standing, and no step here moved that. Not raised as a change |

### Where principle and simplicity disagreed

- **Encapsulation against stack.** As ordinary calls into their own file,
  `FrameListeners_Tell` and `_AreBeingTold` added a frame under every callback. They are
  `static inline` in their header instead: the module still owns them, but the caller can see
  their bodies. The embedded catalogue decided it.
- **Cohesion against stack.** Merging the report walks was right by cohesion, and cost stack
  until the merged function was marked `static inline`.
- **Small functions against call overhead.** `Frame_InitStrike` and `Frame_InitSpare` are small,
  but each states a whole concept once. They make the states' `Init`s tail calls, and stack went
  down, not up.

### The debt, after the clean-up

Before phase 6, `game.c` was 344 lines, with the roll-log and listener extractions deferred on
purpose. Phase 6 ended at 344 again, 36% of the library's `.c` files. After the clean-up it is
**280 lines (216 of code), 29% of 972.**

`game.c`'s reasons to change, before: the pool; a roll's checks; its way along the frames;
creating frames; the roll log, with an edit's checks and splice, and replay; the listener
registry, with its re-entry flag; and reporting changed frames. After: the pool; the checks;
the way along the frames; creating frames; replay; and reporting. Two reasons left: how the
listeners are kept, and how the log is kept and edited.

---

## Conclusion: late binding pays at the boundaries between owners

Across the phases, Kay's properties moved **only where a feature crossed a boundary between
owners**:
- a counting rule the caller owns (phase 1);
- listeners the caller owns (phase 3);
- a game's history, which the caller may now revise (phase 5);
- a machine on another thread, and a display on the far side of a wire (phase 6).

Inside the library, where one party owns everything, the closed Simula-style design held up
against every test. The evidence is in the diff: **of `main`'s 20 library files, the
experiment changed only `game.h` and `game.c`**, the boundary. Phase 6 added six more, all at
the boundary too: the pinsetter (`pinsetter.h`, `pinsetter.c` and its private
`pinsetter_hooks.h`), the fail-stop (`fault.h`, `fault.c`), and a one-constant private header.
The frame
classes, the states, the context, `RollList` and `SlotPool` are untouched since `main`. Phase 2, the one attempt to push late
binding *inward* ("tell, don't ask"), had no feature behind it and was declined.

The cost landed in exactly the same place. Every defect class the experiment found or
guarded against sits at one of those boundaries, and the closed interior never had any of
them:

| Boundary | What late binding cost there |
|---|---|
| The caller's rule | A buggy rule silently corrupted a game (the pins-standing wrap to 255); a `NULL` rule would crash. Now `GAME_ERR_RULE_OUT_OF_RANGE`, and `NULL` refused |
| The caller's listeners | A wrong cast of the `void *` context is undefined behavior no build can catch; a `NULL` callback wasted a slot; a third subscriber overflowed the array for a commit; a roll from inside a listener broke the ordering promise. Now refused, refused, refused, and refused (queued for most of phase 6, then refused again: the queue was principle, not a feature) |
| The caller revising history | A roll number the game hasn't had would have written far past the log; a correction from inside a listener would have replayed mid-notification; a correction can reopen a frame the listeners were told was complete; restoring a rejected correction trusts that the rule is pure. Now `GAME_ERR_NO_SUCH_ROLL`, refused, a `complete = false` message, and a documented requirement the compiler can't check |
| A machine on another thread | Each position published with a weaker ordering is a data race; a roll the game rejected was dropped silently; a full mailbox lost the roll with no one told; a second producer loses rolls, and ThreadSanitizer never catches it. Now release and acquire (each checked by ThreadSanitizer), stop-and-keep, a lost-roll count, and a debug build that stops when two posts overlap |

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
receivers that can decline, and binding that stays open while the program runs. Phase 6 came
nearest: its messages are asynchronous and cross a wire as data. But they are one byte in a
ring and four bytes on a wire, with no selector and no reply, so they are data, not objects. Each would
have meant rebuilding part of a language runtime in C (message selectors, dispatch,
`doesNotUnderstand`). No feature asked for that, so it wasn't built.
