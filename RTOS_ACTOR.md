# RTOS actor experiment: candlepin, FreeRTOS, and no function pointers

A throwaway education branch, `rtos-actor`, cut from the tip of `kay-oo` (ad857ff). It asks what
the design becomes when project code may not use function pointers (JPL Power of Ten rule 9
style), and when FreeRTOS provides concurrency and messaging. Candlepin bowling is the
driving feature.

**The hypothesis to test, not confirm:**
- messaging moves to real task boundaries, closer to Kay's actors;
- variation inside the scorer becomes data plus `switch`, further from Schreiner's OOC;
- much of the re-entrancy defence code disappears.

## Baseline: `kay-oo` at ad857ff, before any change

Measured on 2026-09-28 on Windows: GCC 16.1.0 (WinLibs, MinGW-w64 UCRT), CMake 4.4.3, lizard
1.24.0 and clang-tidy 18.1.8 (the versions CI pins). CI's gates run GCC 13 on ubuntu-24.04, so
frame sizes there differ; the figures below are this host's.

### Tests

| Build | Tests | Result |
|---|---|---|
| Debug | 114 | all pass |
| UBSan (trap mode) | 114 | all pass |
| Release | 112 | all pass (2 are debug-only `assert` death tests) |

ThreadSanitizer runs in CI only; MinGW has no TSan runtime.

### Complexity (ENG-3.1)

| Metric | Value | Where | Limit |
|---|---|---|---|
| Functions (lizard) | 143 | `src/`, `include/` | |
| Average cyclomatic complexity | 1.6 | | |
| Maximum cyclomatic complexity | 5 | `RollEdit_Check` (`roll_edit.c`) | 10 |
| Average NLOC per function | 6.4 | | |
| Maximum NLOC per function | 16 | `Game_CreateWithRule` (`game.c`) | 50 |
| Maximum parameters | 4 | `Frame_InitSpare` (`frame.c`) and others | 4 |
| Maximum cognitive complexity (clang-tidy) | 4 | `RollEdit_Check` | 7 |
| lizard warnings at CI's limits (`-C 10 -L 50 -a 4`) | 0 | | |

Cognitive complexity was measured with the threshold at 0, so every function reports its
score: 52 functions score above 0.

### Stack: the largest frame per file (`-fstack-usage`, ENG-1.3)

Per-function frames, not call-chain depth. All are static (no VLAs, no `alloca`).

| File | Release (-O2) | Debug (-O0) |
|---|---|---|
| `fault.c` | 64 `Fault_Stop` | 48 `Fault_Stop` |
| `frame.c` | 80 `Frame_InitSpare` | 64 `Frame_CopyRolls` |
| `frame_context.c` | 48 `FrameContext_InitTenth` | 48 `FrameContext_Start` |
| `frame_families.c` | 8 | 48 `NewPassingStrike` |
| `frame_listeners.c` | 8 | 32 `FrameListeners_Add` |
| `frame_reporter.c` | 64 `FrameReporter_CatchUpNewest` | 80 `FrameReporter_CatchUpNewest` |
| `game.c` | **224** `Game_CorrectRoll` | 96 `Game_EditRolls` |
| `pinsetter.c` | 80 `Pinsetter_Drain` | 64 `Pinsetter_MarkTaken` |
| `pinsetter_hooks.c` | (inlined away) | 16 |
| `pinsetter_isr.c` | 8 `Pinsetter_Post` | 64 `Pinsetter_Post` |
| `regular_frame.c` | 64 `RegularFrame_PinsStanding` | 48 `RegularFrame_IsStrike` |
| `roll_edit.c` | 96 `RollEdit_Apply` | 80 `RollEdit_RemovesOnlyExistingRolls` |
| `roll_list.c` | 8 | 48 `RollList_At` |
| `roll_log.c` | 16 `RollLog_AppendRange` | 64 `RollLog_AppendPins` |
| `scorecard.c` | 80 `Scorecard_Score` | 64 `Scorecard_Score` |
| `slot_pool.c` | 8 | 64 `SlotPool_Return` |
| `spare_frame.c` | 64 `SpareFrame_Roll` | 48 `SpareFrame_Roll` |
| `strike_frame.c` | 64 `StrikeFrame_Roll` | 48 `StrikeFrame_Roll` |
| `tenth_frame.c` | 48 `TenthStrikeFrame_Roll` | 64 `TenthStrikeFrame_PinsStanding` |

The build's tripwires are `-Wstack-usage=320` for the library and a per-build limit for
`pinsetter_isr.c` (32 bytes in release).

### Release coverage (gcov, `-DCMAKE_BUILD_TYPE=Release -DOO_C_COVERAGE=ON`)

| | Covered | Total | |
|---|---|---|---|
| Lines | 582 | 583 | 99.8% |
| Branches taken | 172 | 174 | 98.9% |

The misses are the two the README already lists: `FrameListeners_TellNewest`'s empty-list
guard (a line and a branch), and the early exit of `Scorecard_ApplyPinsToFrames`'s loop (a
branch).

### Function pointers in `src/` and `include/`

Six function-pointer types, the thing constraint 1 removes:

| Where | Declaration | What it does |
|---|---|---|
| `src/frame.h` | `FrameVtable.roll` | State pattern: a frame's roll behavior |
| `src/frame.h` | `FrameVtable.pins_standing` | State pattern: pins left in the frame |
| `src/frame_families.h` | `FrameStateFactory.new_strike` | Abstract Factory: the strike state for a frame's family |
| `src/frame_families.h` | `FrameStateFactory.new_spare` | Abstract Factory: the spare state for a frame's family |
| `include/game.h` | `PinCountRule` | Strategy: a caller's counting rule (nine-pin no-tap) |
| `include/game.h` | `FrameChangedCallback` | Observer: a listener told about each changed frame |

### Lines of code

`src/` and `include/`: 1,925 physical lines, 1,283 NLOC. The test tree has 2,423 lines.

| File | Lines | NLOC | Functions |
|---|---|---|---|
| `include/bowling_types.h` | 21 | 5 | 0 |
| `include/fault.h` | 16 | 4 | 0 |
| `include/game.h` | 89 | 33 | 0 |
| `include/pinsetter.h` | 52 | 13 | 0 |
| `src/fault.c` | 12 | 9 | 1 |
| `src/frame.c` | 114 | 94 | 18 |
| `src/frame.h` | 94 | 48 | 2 |
| `src/frame_context.c` | 56 | 45 | 10 |
| `src/frame_context.h` | 39 | 23 | 0 |
| `src/frame_families.c` | 33 | 26 | 4 |
| `src/frame_families.h` | 22 | 9 | 0 |
| `src/frame_listeners.c` | 20 | 17 | 2 |
| `src/frame_listeners.h` | 52 | 33 | 2 |
| `src/frame_reporter.c` | 20 | 17 | 3 |
| `src/frame_reporter.h` | 95 | 67 | 9 |
| `src/frame_transition.h` | 17 | 6 | 0 |
| `src/game.c` | 220 | 186 | 18 |
| `src/game_limits.h` | 7 | 0 | 0 |
| `src/pinsetter.c` | 98 | 79 | 7 |
| `src/pinsetter_hooks.c` | 13 | 7 | 1 |
| `src/pinsetter_hooks.h` | 29 | 4 | 0 |
| `src/pinsetter_isr.c` | 37 | 28 | 2 |
| `src/pinsetter_ring.h` | 103 | 60 | 10 |
| `src/regular_frame.c` | 52 | 43 | 5 |
| `src/regular_frame.h` | 14 | 5 | 0 |
| `src/roll_edit.c` | 75 | 62 | 9 |
| `src/roll_edit.h` | 15 | 3 | 0 |
| `src/roll_list.c` | 46 | 39 | 6 |
| `src/roll_list.h` | 25 | 13 | 0 |
| `src/roll_log.c` | 46 | 39 | 6 |
| `src/roll_log.h` | 32 | 13 | 0 |
| `src/scorecard.c` | 86 | 70 | 11 |
| `src/scorecard.h` | 56 | 34 | 4 |
| `src/slot_pool.c` | 42 | 38 | 4 |
| `src/slot_pool.h` | 29 | 12 | 0 |
| `src/spare_frame.c` | 20 | 16 | 2 |
| `src/spare_frame.h` | 15 | 5 | 0 |
| `src/strike_frame.c` | 21 | 18 | 2 |
| `src/strike_frame.h` | 14 | 5 | 0 |
| `src/tenth_frame.c` | 56 | 46 | 5 |
| `src/tenth_frame.h` | 22 | 9 | 0 |

## Decisions

### Task stack sizes: a static call graph, with a painted stack as the cross-check (2026-09-28)

On the FreeRTOS POSIX port a task does not run on the stack buffer it is given.
`pxPortInitialiseStack` (portable/ThirdParty/GCC/Posix/port.c, V11.1.0) only takes that
buffer's size for `pthread_attr_setstacksize`, and pthreads allocates the thread's real stack.
So `uxTaskGetStackHighWaterMark` measures an untouched buffer: a smoke-test task that called
`printf` reported 4,091 of its 4,096 words unused.

That number can't be the ENG-1.3 stack contract. Decided with the user:

- **The contract: static call-graph analysis.** GCC's `-fcallgraph-info=su` gives every
  function's frame and its callees; the worst-case depth from each task entry is computed at
  build time. It is deterministic and needs no target. It can't follow an indirect call, and
  with no function pointers in project code only FreeRTOS's own paths have that gap: they are
  added as a measured allowance, and named.
- **The cross-check: a painted stack on the host.** The task's pthread gets a painted buffer of
  its own (`pthread_attr_setstack`), scanned after the integration tests. It measures real use,
  but only for the paths the tests take, so it can confirm the contract, never set it.
- On a real target `uxTaskGetStackHighWaterMark` works, and would be the third check.

## Mutation testing (Mull), before the strangle

Mull 0.34.1 for LLVM 18, in WSL Ubuntu 24.04 (Mull has no Windows release). `tools/mutation.sh`
builds the library through Mull's clang plugin and runs the whole test binary once per mutant;
`tools/mull.yml` keeps the mutants in `src/`. Two modes, because some code only runs in one kind
of build: `debug` (asserts on) and `release` (`NDEBUG`), which reaches the guards that stand
behind an `assert`.

### The baseline, at 4fa38df (kay-oo's legacy code plus the ten-pin core)

| Mode | Mutants | Killed | Timed out | Survived | Score |
|---|---|---|---|---|---|
| debug | 497 | 435 | 31 | 31 | 93.8% |
| release | 491 | 432 | 34 | 25 | 94.9% |

A mutant that times out is detected: a mutant that loops forever is caught. The release run
kills the six debug survivors that stand behind an `assert` (`roll_list.c`, `roll_log.c`) or
exist only for one (`Scorecard_AllFramesCompleteBefore`).

### What the survivors were

Six were real gaps. Each now has a test that fails against its mutant:

| Where | Mutant | Test added |
|---|---|---|
| `scorer.c`, a fill ball's fresh rack | 42 pins, not 10 | a fill ball of 11 after one clears the rack is rejected |
| `scorer.c`, the edit's ball limit | `>` to `>=` | an edit that leaves exactly 21 balls is accepted |
| `scorer.c`, where new balls go | `index - first` to `index + first` | new balls inserted after ball one land where the edit starts |
| `game.c`, `Game_EditRolls` | the busy mark removed | a roll from inside an edit's notification gets `GAME_ERR_BUSY` |
| `slot_pool.c`, `SlotPool_Find` | `<` to `<=` | a pointer one past the pool is not a game |
| `fault.c`, `Fault_Stop` | `abort()` removed | `Fault_Stop` ends the process with `abort()` (SIGABRT): kills a hand mutant that exits another way (`_Exit(1)`), but not Mull's, which deletes the call from a `_Noreturn` function and so leaves undefined behavior that still dies as expected |

The three legacy ones pin kay-oo's behavior, so the new core can't drift from it unnoticed.

After the six tests (debug mode, 8387bdc): 497 mutants, 440 killed, 31 timed out, **26
survived, 94.8%**, up from 93.8%. Five of the six are killed; the sixth is the `Fault_Stop`
case above.

The rest are not test gaps:

- **Equivalent through `bool` (7).** Mull replaces a value with 42, and a `bool` holds only its
  lowest bit: 42 is even, so `busy = 42` stores `false`, the value it replaced. `frame.c`,
  `game.c` (2), `pinsetter.c` (2), `slot_pool.c`, `scorer.c`.
- **Equivalent by the code around them (9).** Four in `frame_reporter.h` only add loop
  iterations that do nothing; `Frame_CopyRolls` counting down copies the only roll a spare's
  frame ever has; two initial values in `slot_pool.c` and one in `scorer.c` are always
  overwritten before they are read; `scorer.c`'s `>` to `>=` picks between two equal values.
- **Undefined behavior that happens to pass (2).** `roll_edit.c` without `RollLog_Init` reads an
  uninitialized count, and `scorer.c`'s `<` to `<=` in `Scorer_CompleteFrames` reads one
  uninitialized frame. No test can pin these reliably; a memory sanitizer (MSan, Valgrind) would
  see them, and none runs here.
- **Not observable (1).** `Fault_Stop` without its trailing newline still prints the reason.

### What it is for: feedback, not a gate

Mutation testing is occasional feedback to correct course. It is not a gate, and nothing waits
on it.

- **The main feedback loop is fast atomic TDD:** one failing test, only enough code to pass it,
  refactor, and the debug build's tests after every step, in seconds.
- **The main gate is every test passing,** in every build, before each commit (with the
  brief's static checks: lizard, clang-tidy, the function-pointer check).
- **Mutation testing runs once in a while,** by hand: when a phase stops for review, or when a
  question comes up that it can answer, such as whether code is still reached before it is
  deleted. A run takes minutes, far too slow for the loop. What it finds becomes a test, or a
  recorded reason there isn't one.

A nightly run was considered and skipped for now (2026-09-28). GitHub runs scheduled and
manually dispatched workflows only from the default branch's workflow files, so a nightly for
this branch would mean a commit to `main`, which stays untouched.

## The strangle: the frame classes replaced by the core

1. `Scorecard`, the Game facade's frames, became an adapter over the scorer core (4ace724).
   Every existing test passed unchanged, among them the random games against the ten-pin
   reference and the four edit and correction property tests.
2. The mutation checkpoint before deleting anything (debug 483 mutants, release 413) showed the
   frame classes were dead: 66 of the debug run's 88 survivors were in them, because no test
   ran them any more, and the release run, with no debug-only test reaching them, didn't
   mutate them at all. On the live code, 22 survived where 26 had before, all of them the kinds
   already classified above; release scored 95.6%, up from 94.9%.
3. The frame classes, their vtables and state-family factories, and `RollList` were deleted
   (0c50d88): 757 lines. The function-pointer allowlist went from 13 files and 31 uses to 6
   files and 13, all of them the callback facade (`PinCountRule`, `FrameChangedCallback`).

The white-box tests that went with them:

| Test | What it guarded | Why it isn't needed |
|---|---|---|
| `FrameDeathTest.should_stop_a_roll_made_without_a_context` | a vtable call without its context pointer | there is no vtable and no context pointer |
| `RollListTest` (5) and `RollListDeathTest` (2): empty at start, order and sum, full at capacity, a roll past capacity, reading a roll not made | a frame's own fixed list of rolls | the core has no per-frame list: its balls are one array, and a ball after the game is over is refused before anything is written (`should_reject_a_ball_after_the_game_is_over`), so the array never fills past a game's balls |

**A cost found on the way.** The core works everything out from the balls each time it is
asked, which is what makes it a copyable value. Through the adapter, kay-oo's property tests,
thousands of edits each replayed ball by ball with the reporter asking about every frame, made
the test binary about 10 times slower under Mull's instrumentation (0.9 s to 9.7 s; 0.54 s in a
plain debug build). A worst-case execution time question for ENG-1.3, for the phase report.
