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

## Gated CI: rtos-actor is always releasable

Work is pushed only to `integration/rtos-actor`. `.github/workflows/gate.yml` runs every gate
there, as a job with a stable name, and its `promote` job fast-forwards `rtos-actor` to the
same SHA only when all of them pass, with a plain `git push` (never forced), so anything that
isn't a fast-forward is refused. `rtos-actor` has no push trigger of its own: what lands there
was already tested. A ruleset on `rtos-actor` requires the gate jobs, and refuses force pushes
and deletion.

The gate jobs: `build-debug`, `build-release`, `build-ubsan`, `tsan`, `coverage` (at least 95%
of the library's lines, release build), `lizard`, `cognitive-complexity`, `stack-usage-debug`,
`stack-usage-release` (GCC 13), `function-pointers`, `every-commit` (each commit since
`rtos-actor` builds and passes its tests). A whole run takes about 45 seconds of wall time:
the jobs run in parallel, the slowest (`tsan`, `coverage`) at about 40 seconds.

### Proofs: every gate has failed, and the promotion has been refused

Scratch commits were pushed to the integration branch only, and reset away after their run;
none reached `rtos-actor`, which stayed at `8e19adf` through proofs 1 and 2 and the gate
proofs. Proof 4 needed `rtos-actor` to move first: it moved to `c8abe6e`, the commit that first
recorded these proofs, through a normal gated run
([36505773045](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505773045)).

| Proof | Run | Red | `promote` | `rtos-actor` |
|---|---|---|---|---|
| 5: a normal green push | [36505164995](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505164995) | none | fast-forwarded | `8e19adf`, the same SHA as integration |
| 1: a failing test | [36505285384](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505285384) | `build-*`, `tsan`, `coverage`, `every-commit` | skipped | did not move |
| Static gates: a function pointer, and a long, tangled, uncalled function | [36505443556](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505443556) | `function-pointers`, `lizard`, `cognitive-complexity`, `coverage` (at its 95% check), each at its own step; every build and test green | skipped | did not move |
| Stack: a 400-byte frame | [36505554411](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505554411) | `stack-usage-debug`, `stack-usage-release`, and every other GCC build, which carries the same tripwire | skipped | did not move |
| 2: a green tip on a red commit | [36505633406](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505633406) | `every-commit` only | skipped | did not move |
| 3: a red commit, a fast-forward of `rtos-actor`, pushed to `rtos-actor` directly | [36506959168](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36506959168) (the same SHA on integration) | `build-*`, `tsan`, `coverage`, `every-commit` | not reached: the push went straight at `rtos-actor` | refused by the ruleset: "GH013: Repository rule violations found for refs/heads/rtos-actor. 6 of 11 required status checks are failing." Stayed at `edfef2d` |
| 4: a green commit on an older base, after `rtos-actor` moved to `c8abe6e` | [36505912098](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36505912098) | none: every gate passed | failed: GitHub refused the non-fast-forward push, and the job said "rtos-actor moved: it is no longer an ancestor of this commit. Rebase integration/rtos-actor onto rtos-actor and push again, so the result is gated." | stayed at `c8abe6e` |

### The ruleset on `rtos-actor`

`tools/rtos-actor-ruleset.json`, imported in the repository's settings (ruleset 24150541):
active, with an empty bypass list, blocking deletion and non-fast-forward pushes, and requiring
all 11 gate checks from GitHub Actions (app 15368), without "must be up to date".
`GET /repos/mcarlson-sb/oo-bowling-in-c/rules/branches/rtos-actor` shows all three rules in
force.

**Proof 3: a direct push is refused.** A commit with a failing test was put on top of
`rtos-actor`, so pushing it would be a fast-forward, and pushed to the integration branch
([36506959168](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36506959168), red).
Then it was pushed to `rtos-actor` directly:

```
$ git push origin 654562d792861d3d1cca994ab97829ea53a1de94:rtos-actor
remote: error: GH013: Repository rule violations found for refs/heads/rtos-actor.
remote: - 6 of 11 required status checks are failing.
 ! [remote rejected] 654562d792861d3d1cca994ab97829ea53a1de94 -> rtos-actor (push declined due to repository rule violations)
```

The ruleset's other two rules were tried too:

```
$ git push --force origin 8e19adf:rtos-actor
remote: - Cannot force-push to this branch
$ git push origin :rtos-actor
remote: - Cannot delete this branch
```

`rtos-actor` stayed at `edfef2d` through all three. The commit that records this was then
promoted by the gate as usual, with the ruleset in force, so `promote`'s own push passes the
required checks: [36507119638](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36507119638).

### Follow-ups against Fowler's "Continuous Integration"

Checked against the practices in <https://martinfowler.com/articles/continuousIntegration.html>.

- **Fix Broken Builds Immediately.** Fowler: "the best way to fix the build is to revert the
  latest commit". Here a revert can't: `every-commit` tests every commit since `rtos-actor`, so
  the red one stays in range. So the recovery rewrites the integration branch, never
  `rtos-actor`: reset it to `rtos-actor`, re-apply the work corrected, and push with
  `--force-with-lease` against the red tip (`CLAUDE.md` has the steps). Proved: a red commit
  ([36507635496](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36507635496)),
  recovered by those steps, with the README badge as the corrected work; the gate passed and
  promotion resumed ([36507773093](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36507773093)).
  A push whose lease names a tip that has since moved is refused ("stale info").
- **Keep the Build Fast.** A whole gate run takes 45 to 65 seconds, against Fowler's ten
  minutes. After every push, the gate is waited for before the next one; nothing is pushed onto
  a red or running gate (`CLAUDE.md`).
- **Time to green.** When `promote` runs, it reports how long the integration branch was red
  before this run, in the job summary and as a notice. After the recovery above: "integration
  was red for 2m 32s, over 1 red run".
- **Everyone can see what's happening.** The README has the gate's badge. It shows the
  integration branch's runs: `rtos-actor` has none of its own (the badge filtered to it says
  "no status"), since everything on it was gated on the integration branch first.
- **Automate Deployment.** Each promotion publishes `bowling-<sha>`: the release build's
  `libbowling.a` and headers, the ones the gate built and tested (handed over from
  `build-release`, not rebuilt), and the gate summary
  ([36507920475](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36507920475)).
- **Every Push to Mainline Should Trigger a Build.** Deliberately not: `rtos-actor` is only
  ever moved by `promote`, to a SHA that has just passed every gate, so building it again would
  test the same commit twice.

### The gap: Test in a Clone of the Production Environment (a plan, not built)

**Deferred (2026-09-28): skipped for now, by decision.** The plan below stands for when it is
picked up; nothing of it is built.

Fowler: "we want to set up our test environment to be as exact a mimic of our production
environment as possible." Production here is a Cortex-M microcontroller. Every gate runs on a
64-bit Linux host with glibc: `int` is 32 bits on both, but pointers, alignment, the C library,
the ABI and every stack frame differ. The plan is a second stage that closes part of that gap.

**What it would do.**

1. **Cross-compile** the library with `arm-none-eabi-gcc`, warnings as errors, and the stack
   tripwires (`-Wstack-usage`), for a Cortex-M CPU (`-mcpu=cortex-m3 -mthumb`, or M4F with its
   FPU). The target's frames are the ones the ENG-1.3 stack contract is really about; this is
   also where the call-graph analysis decided for task stacks (`-fcallgraph-info=su`) gets its
   numbers.
2. **Run the core's tests under QEMU** on a Cortex-M machine, `qemu-system-arm -M mps2-an385`
   (Cortex-M3), with semihosting for output and the exit status, so a failing test fails the
   job.
3. **Report, not block.** It runs after `promote`, never in its way, and is not a required
   check. It tells us when the target disagrees with the host; it doesn't hold the line.

**How it would be built.**

- **A separate workflow**, triggered by `workflow_run` when the Gate completes successfully on
  the integration branch. A job in `gate.yml` after `promote` would work too, but a failure there
  would turn the Gate's run, and the README badge, red although the promotion succeeded.
- **Toolchain:** Ubuntu 24.04's `gcc-arm-none-eabi` (GCC 13.2, newlib) and `qemu-system-arm`,
  both from apt: about a minute to install, then seconds to build and run.
- **Startup code:** a vector table, a linker script for the machine's memory map, and a reset
  handler that sets up `.data` and `.bss` and calls the test runner. The vector table is an array
  of function pointers the hardware requires; like FreeRTOS's task entries, it would be a named
  exemption in its own shell file, marked for the function-pointer check.

**What it would not prove.** QEMU executes instructions, not cycles: no worst-case execution
time, no cache or bus timing, no real interrupt latency, and no peripherals beyond the machine's
model. It proves the code compiles cleanly for the target, fits its stack limits, and gives the
same answers there; timing still needs real hardware.

**Decisions needed before building it:**

1. **The tests on the target.** GoogleTest needs C++, exceptions and a large libstdc++: it can
   run under newlib and semihosting, but it is heavy, and the thread-based pinsetter tests can't.
   The alternative is a small C test runner (Unity, say) for the pure core only, which is what
   the target would really run. Recommendation: the C runner for the core; GoogleTest stays on
   the host.
2. **The machine.** `mps2-an385` (Cortex-M3), `mps2-an386` (M4, with its FPU) or `mps2-an505`
   (Cortex-M33, TrustZone). Recommendation: the one closest to the real product's MCU.
3. **Its own workflow, or a job after `promote`.** Recommendation: its own workflow, for the badge
   reason above.
4. **The vector-table exemption** to the no-function-pointer rule, in a startup file of its own.

### `promote`: nothing can fail after the push

Once `rtos-actor` has moved, the run that moved it must be green: a red run over a promoted
commit would say the line is broken when it isn't. So the push is `promote`'s last step, and
everything that can fail comes before it: downloading the gated library, writing the summary,
and uploading the release. The summary and the time to green can't fail at all:
`time_to_green.py` catches any error and prints "time to green unavailable", and the shell
around it falls back the same way. The release upload comes before the push too, so if the push
is then refused (a non-fast-forward), `bowling-<sha>` exists for a commit that wasn't promoted,
and that run is red.

**Runs can finish out of order.** Two pushes to the integration branch start two runs, and the
older one can reach `promote` after the newer one has already moved `rtos-actor` past it (the
`promote-rtos-actor` concurrency group runs one promotion at a time, but in the order they
arrive). The older commit is then an ancestor of `rtos-actor`: there is nothing to promote, and
a push would be refused as a non-fast-forward, turning a good run red. So `promote` first checks
`git merge-base --is-ancestor HEAD origin/rtos-actor`, and if it holds, notes "already promoted
by a later run" and stops, green, without pushing. (`CLAUDE.md` says to wait for each gate before
pushing again, which should keep this from happening; the check is for when it does.)

**The release artifacts expire.** `bowling-<sha>` is a workflow artifact, kept for GitHub's
default of 90 days, the most a public repository allows. It is a convenient download for recent
promotions, not a permanent record of releases: the record is `rtos-actor`'s history, and any
release can be rebuilt from its SHA.
