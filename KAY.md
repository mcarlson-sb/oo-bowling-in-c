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

### A note for phase 4

Phase 4, a receiver that can decline a message it doesn't understand (`doesNotUnderstand`),
is optional, "only if features 1 to 3 leave a real need". So far nothing does:
- There is one notification, and every listener wants it.
- There is one rule, and every game needs one.

A need would first appear with a second kind of notification that some listeners don't care
about. Even then, the simplest answer is probably a separate subscription per kind, not a
generic receiver that can decline. The phase 4 review should confirm that before anything is
built.
