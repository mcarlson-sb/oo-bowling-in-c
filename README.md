# OO in C: the Bowling Kata

A ten-pin bowling scorer in C11, written in an object-oriented, pattern-based style. The
problem is small on purpose, so the design is the thing to read. "Object-oriented" is a way
of designing, not a language feature. C has no classes, so every mechanism has to be written
out by hand, and that makes it easy to see how each one works and what it costs.

For a picture-by-picture tour of how the code fits together, see
[ARCHITECTURE.md](ARCHITECTURE.md). For the State pattern at its heart, in depth, see
[STATE_PATTERN.md](STATE_PATTERN.md).

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

Warnings are errors in the library and the tests alike.

## What the code does

The public API is four functions in `include/game.h`: `Game_Create`, `Game_Roll`,
`Game_Score` and `Game_Destroy`. Behind them:

- A **`Game`** holds up to ten frames. Each roll goes to the frames in order until one keeps
  it. If none does, a new frame is started with it.
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
- A roll is rejected, and the game left unchanged, if the game is over
  (`GAME_ERR_GAME_OVER`) or if it knocks down more pins than are standing
  (`GAME_ERR_INVALID_PINS`).

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

Only two headers are in `include/`, because that folder is the library's public API: only
what code *using* the library needs.

| Header | Why it's public |
|---|---|
| `include/game.h` | The API: `Game_Create`, `Game_Roll`, `Game_Score`, `Game_Destroy`, `GameStatus` and the opaque `Game` |
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
- `test/game_test.cpp` includes only `game.h`, as a real caller would. It's a black-box test
  of the public API.
- `test/roll_list_test.cpp` is a white-box test of a private type. It is the one test granted
  `src/`, and `CMakeLists.txt` says why.

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
| **Object Pool** | `Game_Create` and `Game_Destroy` over `s_pool[2]` | Memory is fixed at compile time. Running out is reported (`NULL`), never undefined |
| **Opaque handle** | `Game` | Callers depend only on the API, never on the layout |
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
| `include/game.h`, `src/game.c` | The public API and the `Game` object: the opaque handle, the pool and the roll chain |
| `include/bowling_types.h` | `Pins` and `Score`, the domain's two quantities |
| `src/frame.h/.c` | Abstract base `Frame`: its vtable, shared fields and methods, and `RollResult` |
| `src/roll_list.h/.c` | `RollList`, the value type a frame keeps its rolls and bonus rolls in |
| `src/regular_frame.*`, `src/strike_frame.*`, `src/spare_frame.*` | The states for frames 1 to 9. `RegularFrame` is also where the tenth frame starts |
| `src/tenth_frame.*` | The tenth frame's strike and spare states |
| `src/frame_context.h/.c` | The State-pattern context and the two state families (Abstract Factory) |
| `test/game_test.cpp` | Host tests through the public API: scoring, end of game, game storage, tenth frame, input validation, `NULL` handles |
| `test/roll_list_test.cpp` | Tests of `RollList`, including its bounds checks in debug and release builds |

The git history is a test-driven sequence, with one test per commit. Stepping through it
shows the design growing a test at a time.
