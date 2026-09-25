# OO in C: the Bowling Kata

A ten-pin bowling scorer in C11, written in an object-oriented, pattern-based style. The
problem is small on purpose, so the design is the thing to read. "Object-oriented" is a way
of designing, not a language feature. C has no classes, so every mechanism has to be written
out by hand, and that makes it easy to see how each one works and what it costs.

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

## What the code does

The public API is four functions in `include/game.h`: `Game_Create`, `Game_Roll`,
`Game_Score` and `Game_Destroy`. Behind them:

- A **`Game`** holds up to ten frames. Each roll goes to the frames in order until one keeps
  it. If none does, a new frame is started with it.
- Each frame is a **`FrameContext`** that holds the frame's current **state**:
  - **`RegularFrame`**: where frames 1 to 9 start. On a first-roll 10 it becomes a
    `StrikeFrame`. When its rolls add up to 10 it becomes a `SpareFrame`.
  - **`StrikeFrame`** and **`SpareFrame`** record the next two rolls (strike) or the next one
    (spare) as bonus pins. They also pass those rolls on, because the same rolls start the
    next frame.
  - **`TenthFrame`**: the last frame, from the start. It keeps its own fill balls, because
    there is no next frame.
- Every state scores the same way: its rolls plus its bonus rolls, and 0 while the frame is
  still open.
- A roll is rejected, and the game left unchanged, if the game is over
  (`GAME_ERR_GAME_OVER`) or if it knocks down more pins than are standing
  (`GAME_ERR_INVALID_PINS`).

## Why this is object-oriented

The four pillars of OO, each done with a plain C mechanism:

| Principle | In this code | How it's done in C |
|---|---|---|
| **Encapsulation and information hiding** | Callers can't see or touch a game's internals | `game.h` declares `struct Game` but only `game.c` defines it (an opaque handle). Private helpers are `static`, so no other file can see them |
| **Abstraction** | `Frame` is an abstract type: "something that can take a roll and report its standing pins" | A `const FrameVtable` of function pointers (`src/frame.h`). There is no `Frame` vtable of its own, so a plain `Frame` can't be created |
| **Inheritance** | Each state *is a* `Frame` and reuses its fields and `Frame_Score` | The derived struct holds `Frame base` as its **first member**, so a `StrikeFrame *` is also a valid `Frame *` |
| **Polymorphism** | Callers call `Frame_Roll(frame, pins)`, and the right state's code runs | `Frame_Roll` calls `self->vtable->roll(self, pins)`. Each state points at its own `static const` vtable |

Two refinements on top of those:

- **Shared base-class method.** `Frame_Score` is an ordinary function on the base, with no
  vtable entry, because every state scores the same way. Only behavior that really differs
  goes through the vtable.
- **Default implementation with overrides.** `Frame_AllPinsStanding` is the default
  `pins_standing`. `StrikeFrame` and `SpareFrame` use it as it is; `RegularFrame` and
  `TenthFrame` put their own function in the vtable in its place.

## Design patterns

### State: the core of the design

A frame's behavior depends on what has happened in it so far. Nothing branches on a "frame
type" flag: each kind of frame is its own state object, and the frame switches state.

- **Context:** `FrameContext` holds `current_state` and forwards every call to it.
- **State interface:** `FrameVtable`, with `roll` and `pins_standing`.
- **Concrete states:** `RegularFrame`, `SpareFrame`, `StrikeFrame` and `TenthFrame`.
- **The states drive their own transitions.** `RegularFrame_Roll` calls
  `FrameContext_SetState` when it sees a strike or a spare.
- **No heap.** Each context owns storage for one of each state, so changing state never
  allocates.

### Chain of Responsibility: how a roll finds its frame

`Game_ApplyPinsToFrames` hands each roll to the frames in order. Each frame either keeps it
(`consumed`) or passes it on, and a roll no frame keeps starts a new frame. Unlike the
textbook version, a strike or spare frame can act *and* pass on: it records the roll as a
bonus, then passes it on, because the same roll starts the next frame.

### Supporting patterns

| Pattern | Where | Why here |
|---|---|---|
| **Factory** | `FrameContext_NewStrikeFrame`, `FrameContext_NewSpareFrame`. `Game_AddNewFrame` chooses a regular or tenth-frame start | One place creates each state, in storage the context owns |
| **Object Pool** | `Game_Create` and `Game_Destroy` over `s_pool[2]` | Memory is fixed at compile time. Running out is reported (`NULL`), never undefined |
| **Opaque handle** | `Game` | Callers depend only on the API, never on the layout |
| **Result object** | `RollResult { consumed, pins }` | Says directly whether a frame kept a roll, with no special "magic" values |

## Why write C this way

**Adding behavior means adding a type, not editing branches.** The tenth frame was added as
one new state, `tenth_frame.c`, and `RegularFrame`, `SpareFrame` and `StrikeFrame` didn't
change. In a procedural version the frame type is checked in every function that cares, and
each new case means finding every one of those checks.

**Small units with low complexity.** Each state is one short file with one job. The longest
function is 25 lines, and nesting is never deeper than 2 levels, well inside the limits of
50 lines and 3 levels.

**Internals can change without touching callers.** Three internal redesigns changed no
existing test:
- `Game` went from one static instance to a pool of two, behind the opaque handle.
- Scoring moved out of the three states into the base class.
- `roll()` changed its return type to `RollResult`.

**Suitable for embedded and safety-minded code.**
- No heap: every object lives in fixed storage, sized at compile time.
- Every failure is an explicit status code or `NULL`, with no writes out of bounds.
- The vtables are `const`, so they can live in flash.

**Testable on a PC.** The logic touches no hardware, so the whole suite runs on the host with
GoogleTest and again under the undefined-behavior sanitizer, in about a second.

### The costs, stated plainly

- **Every virtual call is an indirect call,** through a function pointer. It's cheap, but
  not free.
- **Function pointers hide the call graph** from simple static tools, such as stack-depth and
  call-graph analysis. Keep vtables `const` and few.
- **The rules are enforced by convention, not by the compiler.** "Base struct first" and
  "only a state's own methods treat a `Frame *` as that state" are discipline.
- **There is more boilerplate than in C++:** vtable definitions, `_Init` functions and the
  forwarding functions in the context.

## Files

| File | Role |
|---|---|
| `include/game.h`, `src/game.c` | The public API and the `Game` object: the opaque handle, the pool and the roll chain |
| `src/frame.h/.c` | Abstract base `Frame`: its vtable, shared fields, `Frame_Score` and `RollResult` |
| `src/regular_frame.*`, `src/spare_frame.*`, `src/strike_frame.*`, `src/tenth_frame.*` | The four concrete states |
| `src/frame_context.h/.c` | The State-pattern context, and the factories for its states |
| `test/game_test.cpp` | Host tests: scoring, end of game, game storage, tenth frame, input validation |

The git history is a test-driven sequence, with one test per commit. Stepping through it
shows the design growing a test at a time.
