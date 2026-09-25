# OO in C: the Bowling Kata

An object-oriented, pattern-based bowling kata in C11. It is a port of the same design
already written in Java, C++, C# and TypeScript, so the versions can be read side by side.
The point is to show that "object-oriented" is a way of designing, not a language feature.
C just makes every mechanism explicit.

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

## The design

The State pattern. A frame starts as a `RegularFrame` and becomes a `SpareFrame` or a
`StrikeFrame` when its rolls say so. `FrameContext` holds whichever state a frame is in and
passes each call on to it. `Game` runs each roll through the frames in order until one keeps
it. Strike and spare frames pass their bonus rolls on, because those rolls also belong to
the next frame. The tenth frame is a `TenthFrame` from the start. It keeps its own fill
balls, because there is no next frame.

| File | Role |
|---|---|
| `include/game.h`, `src/game.c` | The public API: an opaque `Game` handle |
| `src/frame.h/.c` | Abstract base class `Frame` and its vtable |
| `src/regular_frame.*`, `src/spare_frame.*`, `src/strike_frame.*`, `src/tenth_frame.*` | The four concrete states |
| `src/frame_context.h/.c` | The State-pattern context |
| `test/game_test.cpp` | The spec, ported test for test, plus C-specific tests |

## How each OO idea is written in C

| OO idea | In C | Where to look |
|---|---|---|
| Encapsulation and information hiding | Opaque handle: `game.h` declares `struct Game` but only `game.c` defines it | `include/game.h` |
| Abstract class, virtual methods | A struct led by a pointer to a `const` table of function pointers (the vtable) | `src/frame.h` |
| Inheritance | The derived struct holds its base struct as its **first** member, so a `StrikeFrame *` is also a valid `Frame *` | `src/strike_frame.h` |
| Polymorphism | `Frame_Roll()` calls `self->vtable->roll(self, ...)`, and the right state's code runs | `src/frame.c` |
| Shared base-class method | A plain function on the base struct, with no vtable entry: `Frame_Score()` is written once for all the states | `src/frame.c` |
| Private methods | `static` functions: visible only in their own `.c` file | every `src/*.c` |
| `new` | No heap. Each context owns storage for its states, and games come from a fixed pool | `FrameContext_NewStrikeFrame()`, `Game_Create()` |

## Differences from the other versions

- **Fixed capacity.** Ten frames per game and two games at once. `Game_Roll` returns
  `GAME_ERR_GAME_OVER` once the tenth frame is complete, and `Game_Create` returns `NULL`
  when no game is free, where other languages would just grow.
- **A `TenthFrame` state.** It keeps its fill balls in place of passing them on to extra
  frames. This fixes a scoring defect in the original design: a tenth-frame strike followed
  by 3, 3 scored 22, not 16.
- **Rolls are copied, not shared.** TypeScript's `SpareFrame` keeps a reference to the
  `RegularFrame`'s rolls array. Here each state has its own storage, so they are copied.
- **`roll()` returns a `RollResult`** (`consumed`, or passed on with its `pins`) in place of
  TypeScript's `number | null`. The first port used a `-1` sentinel; the git history shows
  the change.

The git history is the TDD sequence, one test per commit. Stepping through it shows the
design growing test by test.
