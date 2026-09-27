# OO in C: the Bowling Kata

A ten-pin bowling scorer in C11, written in an object-oriented, pattern-based style. The
problem is small on purpose, so the design is the thing to read. "Object-oriented" is a way
of designing, not a language feature. C has no classes, so every mechanism has to be written
out by hand, and that makes it easy to see how each one works and what it costs.

For a picture-by-picture tour of how the code fits together, see
[ARCHITECTURE.md](ARCHITECTURE.md). For the State pattern at its heart, in depth, see
[STATE_PATTERN.md](STATE_PATTERN.md). [KAY.md](KAY.md) logs an experiment in moving the design
toward Alan Kay's idea of objects.

## Build and test

Needs GCC, CMake 3.20 or later, and Ninja. GoogleTest is downloaded by CMake on the first
configure.

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

The same tests again, with undefined-behavior checks:

```sh
cmake -S . -B build-ubsan -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DOO_C_SANITIZE=ON
cmake --build build-ubsan
ctest --test-dir build-ubsan --output-on-failure
```

And as a release build (`NDEBUG`), where the internal asserts compile out and the bounds
checks behind them still hold:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

And under ThreadSanitizer, for the pinsetter's two threads. MinGW ships no ThreadSanitizer
runtime, so this one needs Linux (or WSL). Where address-space randomization is set high, as
on GitHub's runners, it also needs `sudo sysctl vm.mmap_rnd_bits=28` first:

```sh
cmake -S . -B build-tsan -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DOO_C_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Warnings are errors in the library and the tests alike. GitHub Actions runs all four builds
(`.github/workflows/ci.yml`) on every push and pull request.

### Coverage

`OO_C_COVERAGE=ON` instruments the library, but not GoogleTest, for `gcov`. Its `coverage`
target clears old data, runs the tests, and prints a line and branch summary for each
source file:

```sh
cmake -S . -B build-coverage -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DOO_C_COVERAGE=ON
cmake --build build-coverage --target coverage
```

The annotated `*.gcov` files are written to `build-coverage/CMakeFiles/bowling.dir/src/`.
Run it on a release build too (add `-DCMAKE_BUILD_TYPE=Release`, in a separate directory),
because some code only runs in one kind of build. These are the gaps to expect. Anything
else is a real gap:

| Where | Build | Why it isn't covered |
|---|---|---|
| `roll_list.c`: the bounds checks' `return` lines and their branches | debug | In a debug build the `assert` just above stops the program first. The death tests do reach it, but each runs in a child process that `abort()` ends before `gcov` can save its data. The release build covers these lines |
| `game.c`, `frame.c`: an `assert` failing | debug | An assert's failure path is never taken in a passing run. Where a death test does take it, `abort()` ends the process before `gcov` saves the data |
| `game.c`: the loop condition in `Game_ApplyPinsToFrames` stopping early | both | A frame keeps a roll only when it is the latest frame, so the chain never stops with frames still to go. The same bowling fact the frame-reporting count relies on |

The release build reaches 100% of lines and every branch except that loop condition.

## What the code does

The public API is eight functions in `include/game.h`: `Game_Create`, `Game_CreateWithRule`,
`Game_OnFrameChanged`, `Game_Roll`, `Game_EditRolls`, `Game_CorrectRoll`, `Game_Score` and
`Game_Destroy`, and six in `include/pinsetter.h`: `Pinsetter_Create`, `Pinsetter_Post`,
`Pinsetter_Drain`, `Pinsetter_DiscardOldest`, `Pinsetter_RollsLost` and `Pinsetter_Destroy`.
Behind them:

- A **`Game`** holds up to ten frames. Each roll goes to the frames in order until one keeps
  it. If none does, a new frame is started with it.
- Before any frame sees a roll, the game's **`PinCountRule`** decides how many pins it counts
  as. `Game_Create` uses the standard rule: the pins that fell. `Game_CreateWithRule` takes a
  rule from the caller, so a variant of the game can be played without the library knowing
  it. `test/nine_pin_no_tap_test.cpp` plays nine-pin no-tap that way.
- The game keeps a **log of every roll**, as the pins that fell, so the scorer can fix rolls
  entered wrongly. `Game_EditRolls` replaces a range of rolls with new ones: one edit covers
  replacing a roll, inserting rolls and deleting them, so a fix that needs two changes (a
  tenth frame entered as 10, 0, 0 that was really 9, 0) is one edit. `Game_CorrectRoll`, one
  roll for one, is a wrapper around it. An edit replays the whole log, counting each roll
  again with the game's rule. An edit that would make any roll impossible is rejected, and
  nothing changes.
- The game tells up to two **listeners**, set with `Game_OnFrameChanged`, when a frame
  changes: its number, its score, and whether it is complete.
  - After a roll, it tells them about every frame that roll completed, oldest first.
  - After an edit, it tells them every complete frame again, with its new score (a frame
    number they have heard before is an update). A frame the edit reopened is sent with
    `complete = false`. They only ever hear an edit's final state, never a game in between.

  `test/scoreboard_test.cpp` drives a live scoreboard and running stats that way, and
  `test/correction_test.cpp` checks that they stay right through corrections.
  `test/remote_scoreboard_test.cpp` sends each message down a wire as 4 bytes, and rebuilds
  the scoreboard from the bytes alone.

  A listener may read the game. It may also roll: the roll waits in the game's **mailbox**
  (`Game_Roll` returns `GAME_QUEUED`) and is applied once every listener has heard about the
  roll before it, so the listeners still hear about frames in order. A listener may not edit
  the game (`GAME_ERR_EDIT_DURING_NOTIFICATION`).
- The **pinsetter** (`include/pinsetter.h`) is the machine that counts the pins, and in
  firmware it reports each roll from an interrupt handler. It never touches a game: the
  interrupt side only posts the pins to the pinsetter's own mailbox, a lock-free ring with
  room for a whole game's 21 rolls, and the main loop drains them into a game with
  `Game_Roll`. A roll the game rejects is never thrown away, because it is often the right
  one, made to look impossible by an earlier miscount. Draining stops there, returns the
  roll's status and leaves it waiting, until the scorer corrects the earlier roll or discards
  the reported one (`Pinsetter_DiscardOldest`). Each drain takes only the rolls waiting when
  it starts, so the main loop's work per pass is bounded, and a drain from inside a listener
  is refused. A post the full mailbox refuses is counted: `Pinsetter_RollsLost` returns the
  total, and each reader takes its own difference. `test/pinsetter_test.cpp` covers both
  sides, with a real second thread and with a fake interrupt handler fired in the middle of a
  drain.
- Each frame is a **`FrameContext`** that holds the frame's current **state**:
  - **`RegularFrame`**: where every frame starts. On a first-roll 10 it becomes a strike
    state. When its rolls add up to 10 it becomes a spare state.
  - **`StrikeFrame`** and **`SpareFrame`**, in frames 1 to 9, record the next two rolls
    (strike) or the next one (spare) as bonus pins. They also pass those rolls on, because
    the same rolls start the next frame.
  - **`TenthStrikeFrame`** and **`TenthSpareFrame`**, in the tenth frame, take two fill balls
    (strike) or one (spare) and keep them, because there is no next frame.
- Every state scores the same way: its rolls plus its bonus rolls, and 0 until the frame is
  complete.
- A roll is rejected, and the game left unchanged, if:
  - the game is over (`GAME_ERR_GAME_OVER`);
  - it knocks down more pins than are standing (`GAME_ERR_INVALID_PINS`);
  - the game's rule counts it as more pins than were standing (`GAME_ERR_RULE_OUT_OF_RANGE`);
  - it is made from inside a listener, and the rolls already queued there would make more
    than a game can have (`GAME_ERR_TOO_MANY_ROLLS`).

  A roll queued from inside a listener is checked when it is applied. If it is impossible
  then, it is dropped and the queued rolls after it still apply.

  An edit is also rejected for rolls that haven't been made, including an edit that starts
  after the last roll (`GAME_ERR_NO_SUCH_ROLL`): adding a roll is `Game_Roll`'s job. It is
  also rejected if it would make more than 21 rolls (`GAME_ERR_TOO_MANY_ROLLS`), or if it is
  made from inside a listener (`GAME_ERR_EDIT_DURING_NOTIFICATION`).

## Why this is object-oriented

The four pillars of OO, each done with a plain C mechanism, plus information hiding. That one
is often folded into encapsulation, but it's a different idea:

- **Encapsulation** *bundles* data with the operations on it.
- **Information hiding** *conceals* a design decision behind an interface, so the decision
  can change without anything outside having to change.

A postcard is encapsulated: message and address travel together, but anyone can read it. An
envelope is encapsulated *and* hides the message. Code can do one without the other.

| Principle | In this code | How it's done in C |
|---|---|---|
| **Encapsulation** | Each "class" is a struct plus the functions that act on it: `Frame` with `Frame_*`, `RollList` with `RollList_*`, `FrameContext` with `FrameContext_*`. States change a `Frame`'s fields only through `Frame`'s own functions, and `RollList` keeps its array and its count together | A struct and a family of functions that take it as `self`, named with its prefix |
| **Information hiding** | Callers can't see how a game works. `struct Game`'s layout, the State pattern behind it, the frame types and the pool size can all change without any caller changing | An opaque handle: `game.h` declares `struct Game` but only `game.c` defines it. `static` functions are invisible outside their file, and private headers stay in `src/` (see [Public and private headers](#public-and-private-headers)) |
| **Abstraction** | `Frame` is an abstract type: "something that can take a roll and report its standing pins" | A `const FrameVtable` of function pointers (`src/frame.h`). `Frame` has no vtable of its own; only the derived states define one, so every usable `Frame` is one of them |
| **Inheritance** | Each state *is a* `Frame` and reuses its fields and `Frame_Score` | The derived struct holds `Frame base` as its **first member**, so a `StrikeFrame *` is also a valid `Frame *` |
| **Polymorphism** | Callers call `Frame_Roll(frame, context, pins)`, and the right state's code runs | `Frame_Roll` calls `self->vtable->roll(self, context, pins)`. Each state points at its own `static const` vtable |

Four refinements on top of those:

- **Shared base-class methods.** `Frame_Score`, `Frame_AddRoll`, `Frame_Complete` and the
  rest are ordinary functions on the base, with no vtable entry. The base owns its fields'
  rules, and states change those fields only through these functions. Only behavior that
  really differs goes through the vtable.
- **Composition.** A frame's rolls and bonus rolls are each a `RollList`, a small value type
  that keeps an array and its count together and checks its own bounds. `Frame` *has* two
  of them; it doesn't manage raw arrays.
- **Default implementation with overrides.** `Frame_AllPinsStanding` is the default
  `pins_standing`. `StrikeFrame`, `SpareFrame` and `TenthSpareFrame` use it as it is;
  `RegularFrame` and `TenthStrikeFrame` put their own function in the vtable in its place.
- **Template method.** `Frame_Roll` handles a complete frame itself, passing the roll on,
  before it dispatches. So each state's `roll()` only ever sees rolls meant for it.

## Public and private headers

Only three headers are in `include/`, because that folder is the library's public API: only
what code *using* the library needs.

| Header | Why it's public |
|---|---|
| `include/game.h` | The API: `Game_Create`, `Game_CreateWithRule`, `Game_Roll`, `Game_EditRolls`, `Game_CorrectRoll`, `Game_OnFrameChanged`, `Game_Score`, `Game_Destroy`, `GameStatus`, `PinCountRule`, `FrameChangedCallback` and the opaque `Game` |
| `include/pinsetter.h` | The pinsetter's two sides: `Pinsetter_Post` for the interrupt handler, and `Pinsetter_Drain`, `Pinsetter_DiscardOldest` and `Pinsetter_RollsLost` for the main loop, around the opaque `Pinsetter` |
| `include/bowling_types.h` | `game.h`'s signatures use `Pins` and `Score`, and a public header must compile on its own |

Everything else is in `src/` and is private.

**The design lives behind that line.** The frame class hierarchy, the vtable, the states,
the state families and `RollList` are all design decisions. No caller can include
`frame.h`, so no caller can depend on any of them. That is why the internals could be
reworked repeatedly without `game.h` changing. For example, the tenth frame went from one
state to two states and a factory, and a frame's loose fields became two `RollList`s.

**The build enforces it.** In `CMakeLists.txt`:

```cmake
target_include_directories(bowling PUBLIC include PRIVATE src)
```

`include/` is passed on to anything that links `bowling`; `src/` is visible only to the
library's own files. A caller's `#include "frame.h"` isn't found.

**Why private headers exist at all.** Ideally every struct would be defined only in its own
`.c` file, as `struct Game` is in `game.c`. But C needs a struct's full definition wherever
storage for it is allocated. With no heap, objects are held by value, one inside the next:
- `Game` holds ten `FrameContext`s.
- Each `FrameContext` holds one of each state.
- Each state holds a `Frame`.
- Each `Frame` holds two `RollList`s.

So those definitions are shared among the library's files, in `src/`. That makes `Frame`,
`FrameContext` and `RollList` encapsulated but not hidden inside the library: other library
files can see their fields, and only convention stops them writing to them. The hiding
happens one level up, at the library boundary. Where nothing needs the layout, it stays in
the `.c` file:
- `struct Game`: callers only hold a pointer to it.
- `struct FrameStateFactory`: `frame_context.h` only forward-declares it.

**The tests follow the same line.**
- `test/game_test.cpp`, `test/nine_pin_no_tap_test.cpp`, `test/scoreboard_test.cpp`,
  `test/correction_test.cpp`, `test/pinsetter_test.cpp` and `test/remote_scoreboard_test.cpp`
  use only the public headers (through `test/test_support.h`), as a real caller would. They
  are black-box tests of the public API.
- `test/frame_test.cpp`, `test/roll_list_test.cpp` and `test/slot_pool_test.cpp` are white-box
  tests of private types. They are the only tests granted `src/`, and `CMakeLists.txt` says
  why.

## Design patterns

### State: the core of the design

A frame's behavior depends on what has happened in it so far. Nothing branches on a "frame
type" flag: each kind of frame is its own state object, and the frame switches state.

- **Context:** `FrameContext` holds `current_state` and forwards every call to it.
- **State interface:** `FrameVtable`, with `roll` and `pins_standing`.
- **Concrete states:** `RegularFrame`, `StrikeFrame`, `SpareFrame`, `TenthStrikeFrame` and
  `TenthSpareFrame`.
- **The states drive their own transitions.** The context passes itself into `roll()`, and
  `RegularFrame_Roll` calls `FrameContext_SetState` on it when it sees a strike or a spare.
  States don't store a pointer back to their context.
- **No heap.** Each context owns storage for one of each state, so changing state never
  allocates.

### Chain of Responsibility: how a roll finds its frame

`Game_ApplyPinsToFrames` hands each roll to the frames in order. Each frame either keeps it
(`consumed`) or passes it on, and a roll no frame keeps starts a new frame. Unlike the
textbook version, a strike or spare frame can act *and* pass on: it records the roll as a
bonus, then passes it on, because the same roll starts the next frame.

### Abstract Factory: which strike and spare a frame gets

A strike in frame 3 passes its bonus rolls on; a strike in frame 10 keeps its fill balls.
The strike itself is identical; only the state it becomes differs. So each `FrameContext` is
built with a **family** of states: a `const` table of two factory functions
(`struct FrameStateFactory` in `src/frame_context.c`).

- `FrameContext_Init` gives frames 1 to 9 the regular family: `StrikeFrame` and `SpareFrame`.
- `FrameContext_InitTenth` gives the last frame the last-frame family: `TenthStrikeFrame` and
  `TenthSpareFrame`.
- `RegularFrame` just asks its context for "the strike state" or "the spare state" and never
  knows which frame it's in.

The factory table's layout is hidden: `frame_context.h` only forward-declares it.

### Supporting patterns

| Pattern | Where | Why here |
|---|---|---|
| **Object Pool** | `Game_Create` and `Game_Destroy` over `s_games[2]`, with the in-use bookkeeping in `SlotPool` (`src/slot_pool.c`) | Memory is fixed at compile time. Running out is reported (`NULL`), never undefined |
| **Opaque handle** | `Game` | Callers depend only on the API, never on the layout |
| **Observer** | `Game_OnFrameChanged`: a callback and a context pointer, two listener slots | Subscribers learn about changed frames without polling, and `Game` doesn't know who they are |
| **Strategy** | `PinCountRule`, given to `Game_CreateWithRule` | The caller decides how a roll is counted, at run time, without the library containing the variant |
| **Result object** | `RollResult { consumed, pins }` | Says directly whether a frame kept a roll, with no special "magic" values |

## Why write C this way

**Adding behavior means adding a type, not editing branches.** The tenth frame's rules are
two new states and one new factory table, in `tenth_frame.c` and `frame_context.c`.
`RegularFrame`, `StrikeFrame` and `SpareFrame` didn't change. In a procedural version the
frame type is checked in every function that cares, and each new case means finding every
one of those checks.

**Small units with low complexity.** Each state is one short file with one job. The longest
function is 21 lines, and nesting is never deeper than 2 levels, well inside the limits of
50 lines and 3 levels.

**Internals can change without touching callers.** These internal redesigns changed no
existing test:
- `Game` went from one static instance to a pool of two, behind the opaque handle.
- Scoring moved out of the states into the base class.
- `roll()` changed its return type to `RollResult`, and later took the context as an
  argument.
- The tenth frame went from one state that counted its own phases to a family of explicit
  states.

**Suitable for embedded and safety-minded code.**
- No heap: every object lives in fixed storage, sized at compile time.
- Every failure is an explicit status code or `NULL`.
- Array bounds are checked in one place, `RollList`, in every build.
- The vtables and factory tables are `const`, so they can live in flash.

**Testable on a PC.** The logic touches no hardware, so the whole suite runs on the host with
GoogleTest and again under the undefined-behavior sanitizer, in about a second.

### The costs, stated plainly

- **Every virtual call is an indirect call,** through a function pointer. It's cheap, but
  not free.
- **Function pointers hide the call graph** from simple static tools, such as stack-depth and
  call-graph analysis. Keep vtables `const` and few.
- **The rules are enforced by convention, not by the compiler.** "Base struct first" and
  "states change base fields only through the base's functions" are discipline.
- **The public/private line is kept by the build, not the language.** Anyone who adds `src/`
  to their own include path can reach the internals. `PRIVATE src` makes that a deliberate
  choice, not an accident.
- **Type names don't add type safety.** `Pins` and `Score` are C `typedef`s, so they tell the
  reader what a value is, but the compiler still sees plain integers: passing a roll count
  where `Pins` is expected compiles cleanly. A one-field struct would catch that, at the cost
  of a helper for every piece of arithmetic. Here `Game_Roll` checks every pin count as it
  comes in.
- **There is more boilerplate than in C++:** vtable and factory definitions, `_Init`
  functions, the forwarding functions in the context, and `(void)context;` in states that
  don't need it.

## Files

| File | Role |
|---|---|
| `include/game.h`, `src/game.c` | The public API and the `Game` object: the opaque handle, the pool, the roll chain, the listeners and the mailbox for rolls made from inside them |
| `include/pinsetter.h`, `src/pinsetter.c` | The pinsetter: a lock-free ring of 21 rolls between the interrupt handler that posts them and the main loop that drains them into a game |
| `src/game_limits.h` | `GAME_MAX_ROLLS`, shared by the game's roll log and the pinsetter's mailbox |
| `src/game_internal.h` | `Game_IsNotifying`, which the pinsetter asks before draining; private to the library |
| `include/bowling_types.h` | `Pins` and `Score`, the domain's two quantities |
| `src/frame.h/.c` | Abstract base `Frame`: its vtable, shared fields and methods, and `RollResult` |
| `src/roll_list.h/.c` | `RollList`, the value type a frame keeps its rolls and bonus rolls in |
| `src/slot_pool.h/.c` | `SlotPool`, which tracks which of a pool's slots are in use, for the games and for the pinsetters |
| `src/regular_frame.*`, `src/strike_frame.*`, `src/spare_frame.*` | The states for frames 1 to 9. `RegularFrame` is also where the tenth frame starts |
| `src/tenth_frame.*` | The tenth frame's strike and spare states |
| `src/frame_context.h/.c` | The State-pattern context and the two state families (Abstract Factory) |
| `src/frame_transition.h` | The three context functions a state uses to change state, kept apart from what `Game` uses |
| `test/game_test.cpp` | Host tests through the public API: scoring, end of game, game storage, tenth frame, input validation, `NULL` handles |
| `KAY.md` | The log of the Kay-style OO experiment on the `kay-oo` branch |
| `test/nine_pin_no_tap_test.cpp` | A client that plays nine-pin no-tap by supplying its own `PinCountRule`, plus checks on rules that misbehave |
| `test/scoreboard_test.cpp` | Clients that subscribe to changed frames: a live scoreboard and running stats |
| `test/correction_test.cpp` | A scorer correcting and editing rolls: rescoring, the rule applied again on replay, rejected edits, listeners told only the final state, and property tests against a fresh game under both rules |
| `test/pinsetter_test.cpp` | The pinsetter: rolls posted and drained in order, a drain stopped at an impossible roll and resolved, a whole game waiting, lost rolls counted (two readers, and the count wrapping), a fake interrupt in the middle of a drain, a drain refused from inside a listener, `NULL` handles, and a real second thread (run under ThreadSanitizer in CI) |
| `test/remote_scoreboard_test.cpp` | A listener that writes each message into a byte buffer, and a decoder that rebuilds the scoreboard from the bytes alone |
| `test/test_support.h` | What the black-box tests share: `GameHandle`, `RollAll`, `RunningStats` and the client-side no-tap rule |
| `test/roll_list_test.cpp` | Tests of `RollList`, including its bounds checks in debug and release builds |
| `test/slot_pool_test.cpp` | Tests of `SlotPool` |
| `test/frame_test.cpp` | A debug-build check that `Frame_Roll` enforces the `roll()` contract |

The git history is a test-driven sequence, with one test per commit. Stepping through it
shows the design growing a test at a time.

## License

[MIT](LICENSE).
