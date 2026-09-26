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
  `include/` 55 → 70). The client test is 85 lines.
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
