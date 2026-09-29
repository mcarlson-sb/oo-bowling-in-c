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
| `cognitive-complexity` re-proved on the runner's own clang-tidy 18.1.3, after it replaced the downloaded 18.1.8: a short, deeply nested function (cognitive complexity 10, within lizard's limits) | [36515744472](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36515744472) | `cognitive-complexity` only, at its own step | skipped | did not move |
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

**Proved:** a commit pointed `time_to_green.py` at a URL that doesn't resolve. The run stayed
green and promoted `rtos-actor` to it
([36509487693](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/runs/36509487693)), with
the notice "time to green unavailable (URLError: Name or service not known)". The next commit
took the bad URL out again.

## Phase 1 report: a pure, data-driven core, and candlepin

**What changed.** One generic scorer, `include/scorer.h` and `src/scorer.c`, replaced
kay-oo's State pattern: the frame classes, their vtables and state-family factories, and
`RollList`. It is a plain value, the variant and rule it plays and the balls it has taken, and
it answers every question by walking those balls through one `static const` row of rules, with
the lane's phase as an enum and a `switch`. It has no callbacks: a roll or an edit returns its
status and writes the frames it changed to a buffer the caller supplies. The counting rule is
data too: `CountRule`, an enum (`SCORER_COUNT_PINS_DOWN`, `SCORER_COUNT_NO_TAP`), where kay-oo
took a caller's function. The `Game` facade runs on the core through `Scorecard`, now an
adapter, and every existing test passes unchanged. Candlepin is a second row.

### The hypothesis: both games from the same few parameters

**It held.** Both games are described by exactly the parameters proposed: balls per frame,
pins per rack, frames per game, the most balls a game can take, and the bonus balls owed by
the ball that cleared the rack (ten-pin 2, 1; candlepin 2, 1, 0). The tenth frame needed no
parameter of its own: its fill balls are the same bonus, "after clearing on ball k, throw
bonus(k) more", on a fresh rack whenever one is cleared.

The evidence: with the candlepin row added, all 22 of the brief's acceptance examples
passed with no code change, and so did 10,000 random games against an independent candlepin
reference, checked after every ball. Because they passed at once, each field of the row was
broken in turn to see them fail: balls per frame 3 to 2 (17 tests fail), a strike earning one
bonus ball (8), a ten-box earning one (4), 9 frames (16). The one that failed nothing was the
maximum, 30 to 29, which exposed a missing test: an edit that leaves exactly 30 balls. It was
added, and now fails that mutation.

### Replacing `PinCountRule` with data: what it loses

- **An open set becomes a closed one.** A caller could write any rule; now a rule exists only if
  the library has an enum value and a `case` for it. Nine-pin no-tap and "one pin left clears
  the rack" are both expressible, but adding a third means changing the library. In Cook's
  terms, the rule went from an object to an ADT.
- **What it gains:** a rule can't be impure, count out of range, or call back into the game. The
  tests that exist because rules are code (`GAME_ERR_RULE_OUT_OF_RANGE`, the `NULL` rule, the
  impure rule's fail-stop, a rule that rolls or subscribes from inside itself) describe states
  the core can't reach. They still pass, because the `Game` facade still takes a
  `PinCountRule`: that is the strangled API, kept until callers switch.

### Metrics against the baseline (kay-oo at ad857ff)

| | Baseline | Now |
|---|---|---|
| Tests (debug / release) | 114 / 112 | 159 / 158 |
| Functions (lizard) | 143 | 117 |
| Average / maximum cyclomatic complexity | 1.6 / 5 | 1.9 / 6 (`Scorer_CheckEdit`) |
| Average / maximum NLOC per function | 6.4 / 16 | 7.6 / 21 (`Scorer_Roll`) |
| Maximum parameters | 4 | 4 |
| Maximum cognitive complexity | 4 | 5 (`Scorer_CheckEdit`, `Scorer_ReportAll`) |
| Largest stack frame, release (GCC 16) | 224, `Game_CorrectRoll` | 256, `Scorer_Edit` (`Game_CorrectRoll` still 224) |
| Release line / branch coverage | 99.8% / 98.9% | 99.5% / 99.1% |
| Lines in `src/` and `include/` (files) | 1,925 (41) | 1,729 (27) |
| Function-pointer types / uses / files | 6 / 31 / 13 | 2 / 13 / 6, all the callback facade |

Every gate is inside its limit. The two uncovered lines are `case LANE_OVER` in the lane's
`switch`, which no walk reaches (`Scorer_Roll` refuses a ball once the game is over); the case
is kept because `-Wswitch` wants every value handled. Stack figures are single frames, not
call chains: `Scorer_Edit` calls the replay, which calls `Scorer_Roll` and its lane, so the
deepest chain is larger. Measuring chains is the call-graph analysis decided for phase 2.

**Lines deleted against added** (kay-oo to now): `src/` and `include/` +560, -756; the tests
+880, -163.

### Surprises

1. **The perfect-game test passed in debug and release by accident**, writing an 11th and 12th
   frame past an array. Only the UBSan build caught it.
2. **The stack tripwire fired on a data change.** Keeping each ball's counted value made the
   lane 30 bytes bigger, and `Scorer_Edit`, with two lanes live at once, went to 336 bytes
   against the 320 limit. Counting before the replay, so only one is live, brought it to 256.
3. **Recomputing is a real cost.** The core works everything out from the balls on every
   question: it is what makes it a copyable value, and what made the rejected edit's undo free
   (a scratch copy, which phase 5b couldn't use). Through the adapter, kay-oo's property tests
   made the test binary about 10 times slower under Mull's instrumentation. With at most 30
   balls each walk is short, but it is a worst-case execution time question for ENG-1.3.
4. **Candlepin needed no code.** The whole variant is a row, and the tests that describe it went
   green before any line of the scorer changed.
5. **The constitution argues the other way.** ENG-3.1's own remedy for a growing `switch` is "a
   function-pointer table", exactly what this experiment forbids. The `switch`es here stay small
   (three phases, two rules) because the variation moved into data, not into cases.
6. **The FreeRTOS POSIX port can't measure a task's stack** (`uxTaskGetStackHighWaterMark`
   reads a buffer the task never runs on): found early, and settled in the decisions above.

### Mutation feedback at the phase stop

| Mode | Mutants | Killed | Timed out | Survived | Score | Mutants' time |
|---|---|---|---|---|---|---|
| debug | 408 | 367 | 15 | 26 | 93.6% | 1 m 25 s |
| release | 402 | 354 | 26 | 22 | 94.5% | 1 m 47 s |

Every survivor is in a class already recorded: 7 equivalent through `bool`, 11 equivalent by
the code around them, 5 behind an `assert` or only for one (which the release run reaches), 2
in `Fault_Stop`, and 3 that read memory the walk didn't write and happen to pass (a memory
sanitizer would see them).

**Getting it this fast.** The first run at this stop took 46 minutes, and a clean run of the
test binary under Mull took about as long as its 30-second timeout, so a slow run could be
scored as a timed-out mutant: a kill that wasn't one. Per test, 78% of a run was kay-oo's four
property tests of the `Game` facade's edits (8.9 s of 11.3 s; the other 149 tests took 0.07 s).
Leaving them out lost three kills, and each was dealt with:

- A facade deletion that passes no pins at all: now an example test, which kills its mutant.
- Two mutants of the facade's bound check, `index < frame count`, that let the reporter ask
  about frames not started. `Scorer_Frame` then read frames the walk never wrote: undefined
  behavior, which the property tests' random games only sometimes turned into a wrong answer. The
  fix went into the core: `Scorer_Frame` now answers "not complete, 0" for any frame not
  started (the UBSan build trapped on its new test before the fix). The two mutants are now
  equivalent, and an example test pins the behavior.

So `tools/mutation.sh` leaves those four tests out by default, the timeout is back to 10 s, and
the CPU cap on looping mutants is 30 s: a run of the binary takes about 2.5 s. A `diff <ref>`
mode mutates only the lines changed since a ref: against `rtos-actor`, after the `Scorer_Frame`
fix, it found 4 mutants and killed 3 in 1.6 s (9 s with the build), and the survivor was that
fix's own bound, `>=` to `>`, which reads one frame not written: the undefined-behavior class
above.

### Decisions for phase 2

1. **When the callers switch** from the callback facade (`PinCountRule`, `FrameChangedCallback`)
   to the new interface, which removes the last six allowlisted files. Phase 2's subscriber
   queues are the natural point. The tests that only exist because rules and listeners are code
   would then be deleted, each with the hazard it guarded, as the frame classes' were.
2. **The recompute cost:** accept it (at most 30 balls a walk), or have the scorer keep its
   frames as it goes. That keeps it a value, but it is more state to keep right. Measuring WCET
   on a target would decide it; that stage is deferred.
3. **A23** (a frame listener hears frame 1 change from 13 to 10) is phase 2's: in the pure core
   it is already an edit's frame events, and a subscriber queue is where it becomes a listener.

## Phase 2: the FreeRTOS shell

### The walking skeleton

FreeRTOS-Kernel V11.1.0, fetched and built on Linux (`OO_C_RTOS`, on by default there, off on
Windows, where the POSIX port doesn't run), with static allocation only
(`configSUPPORT_DYNAMIC_ALLOCATION 0`) and `configASSERT` routed to `Fault_Stop`. The shell is in
`rtos/`. The integration tests are an executable of their own, `rtos_tests`: each starts the
scheduler, which is state for the whole process, and ctest runs each in its own. The first one
creates a static task that ends the scheduler, and `vTaskStartScheduler` returns to the test.

**ThreadSanitizer and the POSIX port.** The port simulates interrupts with signals delivered
to pthreads, and guards the kernel's data by masking them: synchronization TSan can't see, so
it reports the kernel racing with itself (six reports for one task). `tools/tsan-freertos.supp`
suppresses those by top frame only (`race_top`): every task's stack has the port's thread start
at its bottom, so a plain `race:` rule would hide races in the shell's own task code too. That a
race in our code still gets through was shown by the skeleton's first version: its task wrote a
plain `bool` that the test read after the scheduler stopped, and TSan reported it, in the test's
own frame. The handoff is ordered in practice, but nothing TSan can see orders it, so results
now cross out of a task as atomics. With the suppressions the skeleton is clean under TSan in 20
runs of 20; without them it has 5 reports.

**A red run on the way.** The coverage job failed: its target built only `bowling_tests`, and
`rtos_tests` also needs gcov's runtime at link time. The local gates hadn't caught it because
they didn't run the coverage build; they do now. Recovered by the documented procedure: the fix
folded into the red commit, pushed with `--force-with-lease`, and promoted; the line was red for
5 m 13 s.

### Kay-oo's full-mailbox policy is drop-newest

The phase 2 brief describes today's policy as discarding the oldest roll. The code refuses the
newest: `Pinsetter_Post` posts nothing, and counts the roll lost, when the mailbox is full
(`include/pinsetter.h`, `src/pinsetter_ring.h`). `Pinsetter_DiscardOldest` is a scorer's command,
to throw away a waiting roll that turned out to be a glitch; it isn't the full policy.

### A correction to the mutation figures: too many workers hid survivors

Every mutation run until now used one Mull worker per core, 24. With that many test binaries
running at once, they slow each other past Mull's 10-second timeout, and Mull reports a mutant
it couldn't finish as timed out, which it scores as killed. The same binary, with the new game
actor's 115 mutants, gave 16 killed, 96 timed out and 3 survived at 24 workers, and 94 killed, 3
timed out and 18 survived at 4. So the scores above, from 24-worker runs, may count some
survivors as caught. `tools/mutation.sh` now uses 4 workers (`MULL_WORKERS` overrides it).

Measured again at 4 workers, at `f4e69fd` (phase 1's code and the game actor):

| Mode | Mutants | Killed | Timed out | Survived | Score | Time |
|---|---|---|---|---|---|---|
| debug | 518 | 472 | 15 | 31 | 94.0% | 7 m 20 s |
| release | 512 | 469 | 16 | 27 | 94.7% | 8 m 16 s |

The survivors are the classes above, and 5 in the game actor: 4 equivalent (a dead initial
value, `NULL` or a pointer when there are no new balls, and two in the catch-up's bound, which
`Scorer_Frame` now answers past the frames started), and one read past a full held list, which
the UBSan build traps on but Mull's build doesn't. Also found: Mull's `diff` mode skips files
new since the ref altogether, so it reported no mutants for the whole new actor; a new module
is checked with `tools/mutation.sh only <regex>`.

### Comment clutter: names, checks and tests instead

Before the RTOS shell, the 114 comments in the core and the actor (`bowling_types.h`,
`bowling_status.h`, `scorer.h`, `scorer.c`, `game_actor.h`, `game_actor.c`) were sorted by
what could replace them:

| What it was | Count | Became |
|---|---|---|
| A name's job: restating a function or field, or a section divider | 60 | A name, or nothing |
| A claim about behavior | 24 | A test, an assert or a derived constant; most were already tests |
| History or design notes | 8 | This file, where most already were |
| The public contract (ENG-3.6), or a real "why" | 16 | Kept, trimmed |
| Include guards, and the facade's own statuses, going with it | 6 | Left |

One claim was already false: `GAME_ERR_TOO_MANY_ROLLS` said an edit would leave "more than 21
rolls" long after candlepin made it 30. Nothing failed when it went stale. The claims nothing
checked became checks:

- **Frames complete oldest first**, which the complete-frame count and the actor's catch-up
  both rely on: a test over random games of both variants.
- **The outbox's capacity**: a test sends the worst case, an edit that reopens all ten frames and
  lets through held rolls that complete them again before holding the next, to two subscribers,
  and it fills the outbox exactly. The old formula, from a comment's "a notice or two", had room
  for 45; the most is 43. An assert stops any write past it.
- **At most one event per frame**, **no ball on a lane that is over** and **an edit's removals
  within the balls**: asserts. Removing only balls the game has is now worked out without the
  subtraction that wrapped at ball 0, so its precondition comment went with the hazard.
- **The longest game**, 21 and 30, is worked out from each row's frames and balls
  (`LONGEST_GAME`), and the bonus table is indexed by the ball that cleared the rack
  (`CLEARED_BY_STRIKE`, `_SPARE`, `_TEN_BOX`), where comments said what `[0]`, `[1]` and `[2]`
  meant.

A note that had no home here yet: `Scorer_Edit` checks an edit with the same rules, in the same
order, as the `Game` facade's edits, so the two report the same status for the same bad edit
while both exist.

### One job per type, and functions at one level

After the comments, the structure. The actor handled two data structures as raw arrays inside
its message handlers: the held rolls, a list written out by hand, and the subscribers, appended
in one handler and searched and swap-removed in another. They are `HeldRolls` and
`Subscribers` now, and the handlers only decide what to reply and whom to tell. Three handlers
each rolled into the scorer and published the events, so that is one step, `GameActor_Play`,
and the pinsetter's handler reads "if nothing is held, play it or hold it; else hold it behind
the rest".

In the scorer, functions mixed named steps with raw field changes. The rack was reset in two
places, the lane was started field by field inside the walk, and a roll's report and an edit's
report were two loops doing the same thing. Each is a named step now. The score and a frame's
answer come from the count of complete frames, which the oldest-first test makes safe.

The scorer's edit arithmetic (`RollEdit_*`, and the edited ball) is its own job, and waited for
the facade's `src/roll_edit.c`, which had the name and did the same job over `RollLog`, to go.
It is now `src/roll_edit.c`, private to the core.

### The shell: one game task, three queues, one notification

`rtos/game_shell.c` owns the one `GameActor`, in a statically allocated task of its own:

| Into the game task | Holds | Filled by |
|---|---|---|
| The command queue | 4 `GameMessage`s | `GameShell_Send`, from any task |
| The pinsetter's queue | 32 rolls, static-asserted to hold a whole game (30) | `GameShell_PinsetterCountedFromIsr` |
| The lost report | 1 total, overwritten | The same, when the pinsetter's queue is full |

Every sender posts to its queue and then gives the game task its notification. The task waits on
that and then handles everything waiting: the pinsetter's rolls, then its lost report, then a
command. The plan was a queue set, but FreeRTOS V11.1.0 has only `xQueueCreateSet`, which needs
the heap this experiment forbids, and there's no `xQueueCreateSetStatic`. A notification does the
same job with no allocation. Its count is cleared on wake, and the task drains every queue before
it waits again, so a post that lands during the drain wakes it once more and is never missed.

Out of the game task, every output goes to the queue its `to` names: a caller's reply queue, or a
subscriber's. Each is sent with no wait. One its queue can't take is dropped and counted
(`GameShell_OutputsDropped`, a C11 atomic, since any task may read it). So a caller's reply queue
must hold as many replies as it has requests outstanding. A full pinsetter queue drops the newest
roll: the interrupt counts it in a variable only it touches, and overwrites the one-slot report
with the new total, so the game task never reads the interrupt's variable.

**The simulated interrupt, and its limits.** The tests' interrupt is the highest-priority task.
When fired, it counts its rolls back to back, and nothing lower runs until it's done: that is how
33 rolls overrun a queue of 32. It calls the real `FromISR` functions, and yields as an interrupt
would, through `portYIELD_FROM_ISR`. What it can't do:
- preempt a task mid-instruction, or inside a critical section;
- nest;
- run on a real interrupt stack.

So it tests the queue and notification protocol, not interrupt latency or interrupt-stack depth.
Phase 2's stack sizing (from the call graph) covers the depth.

**Gates.** The function-pointer check, lizard, cognitive complexity and the stack tripwire now read
`rtos/` as well. The first two of those need FreeRTOS's headers, so their CI jobs configure first,
which fetches the kernel. The function-pointer check was proved on `rtos/`: with the task entry's
exemption marker removed, it fails on `rtos/game_shell.c`, "address of function 'GameShell_Task'
taken". The coverage gate measures the shell with the library: `rtos/game_shell.c` is at 100%
of its 57 lines, from the integration tests. Fetched code, under `_deps/`, is never counted.

### The switch-over: the facade and the pinsetter deleted

Nothing but tests called kay-oo's `Game` facade or its pinsetter, so switching the callers meant
switching the tests. Every test of the legacy code was sorted before anything was deleted:

| Legacy tests | What pins the behavior now |
|---|---|
| `GameTest`: scoring, rejections, the tenth frame, fill balls, a perfect game | `TenPinScorerTest`, and the random games against the independent reference |
| `ReferenceScorerTest`, random games, plain and no-tap | The scorer's random games against the same reference, ten-pin and candlepin |
| `NinePinNoTapTest`, four examples | `NoTapScorerTest`, the same four |
| `CorrectionTest`, `EditRollsTest`: an edit's checks, reopened frames, a deletion, a rejected edit telling nothing | `TenPinScorerEditTest` |
| The same, with no core test yet: a strike really 9 then 1, a tenth frame with a ball too many, the first and last balls of the longest game, a no-tap replay | **Ported** to `TenPinScorerEditTest` and `NoTapScorerTest`; proved by mutation |
| `CorrectionPropertyTest`, `EditRollsPropertyTest`: an edit leaves listeners as a fresh game would | **Ported** as a property over 3000 random edits for ten-pin, no-tap and candlepin, with the events checked too; proved by mutation |
| `ScoreboardTest`, `LateListenerTest`: frames told oldest first, the tenth after its fill balls, two subscribers, room for two, a late subscriber caught up | `GameActorTest`'s subscriber tests |
| `PinsetterTest`: hold at a rejected roll, discard, rolls after the game, a whole game held, lost rolls | `GameActorPinsetterTest`, its mirrors, and the shell's end-to-end held-roll test |
| `PinsetterThreadTest`, and a correction while rolls wait | `GameShellTest`, on the POSIX port |

What was tested because only the legacy design could get it wrong, and is gone by construction:

- **Re-entry** (`GAME_ERR_BUSY`): a listener that rolls, edits, subscribes or destroys its game,
  or drains a pinsetter from inside a drain. There are no callbacks: the actor finishes one
  message before it takes the next.
- **Handles**: NULL games, destroying twice or while busy, the slot pool running out, a
  pinsetter destroyed while draining. The actor is a value the shell owns, and the shell has no
  handles to hand out.
- **A caller's counting function**: counting more pins than stand, a NULL rule, an impure rule
  that makes a rejected edit impossible to undo. The rule is a closed enum, and an edit replays
  into a copy, so a rejected one is never undone.
- **Overlapping interrupt posts**, which could lose a roll in the one-producer mailbox. A
  FreeRTOS `FromISR` queue takes any number of interrupt sources.
- **Two readers of the lost count across its wrap.** The actor reports the running total as an
  event.

`GAME_ERR_NULL_GAME`, `GAME_ERR_RULE_OUT_OF_RANGE` and `GAME_ERR_BUSY` went with them.

Lost on purpose, as the phase 1 report said: a caller-supplied counting rule (the one-pin-left
rule's test), the no-tap running-average listener, and the remote scoreboard rebuilt from bytes,
which a queue's copy of a `GameOutput` now does.

The function-pointer allowlist's legacy section is empty. The only function pointer in the
project is the game task's entry function, which FreeRTOS requires. 114 legacy tests went; 5 were
ported, one of them a property over three variants. The interrupt side's stack tripwire was
legacy-only, so before its file went, the shell's interrupt side moved to a file of its own, with
its own limit.

### Task stacks: the contract from the call graph, and the painted cross-check

As decided (above, 2026-09-28): the contract is the static call graph, and a painted stack
cross-checks it.

**The contract.** `tools/stack_depth.py` reads every call graph GCC 13 writes with
`-fcallgraph-info=su`, the FreeRTOS kernel's included. For each entry, it adds the entry's frame
to the deepest of its callees', and fails on anything it can't bound: recursion, a dynamic frame
with no bound, or an indirect call. The only indirect call in the build is FreeRTOS's task start
(`prvWaitForStart`) calling the entry itself.

Two things are outside the graph, and each gets an allowance:
- **A call into the C library or pthreads,** which the POSIX port makes: 1024 bytes a call.
- **One asynchronous frame, anywhere:** the port's tick and context switches are signals, whose
  handlers run on the task's own stack. 2048 bytes. On a target, this is the interrupt frame.

| Entry | Deepest path, release / debug | With the allowances | Budget |
|---|---|---|---|
| `GameShell_Task` | 880 / 1296 | 3952 / 4224 | 4608 |
| `GameShell_PinsetterCountedFromIsr` | 160 / 272 | 3232 / 3344 | 3584 |
| `GameShell_Send`, on its caller's stack | 224 / 400 | 3296 / 3472 | 3584 |

The game task's deepest path, in debug, is `GameShell_Task` → `GameActor_Handle` →
`GameActor_Edit` → `Scorer_Edit` → its replay's `Scorer_Roll` → `Scorer_Lane` → the lane's walk.
The deepest work is the edit's replay, not the kernel. The budgets are `#define`s in
`rtos/game_shell.h`, read by both checks. The task's stack is static-asserted to hold its budget,
and on this host it is the port's minimum anyway: 16 KiB, a pthread's `PTHREAD_STACK_MIN`. CI's
stack-usage jobs, debug and release, build with the call graph and run the check. It was proved to
fail on a budget under the depth, on an indirect call and on recursion.

**The cross-check.** On the POSIX port a task runs on a pthread stack of the port's own, so
FreeRTOS's high-water mark measures nothing. `rtos/posix_stack.c` stands in for it:
- the game task paints its own pthread stack first thing;
- a test drives the outbox's worst case through the shell: an edit that reopens every frame and
  lets 12 held strikes through before holding the 13th, told to two subscribers;
- the test then checks the deepest byte touched against the budget.

Measured: 1079 bytes in debug, 1583 in release, 711 under ThreadSanitizer. In release that is 703
bytes over the static path's 880: the C library and the signal frames, which the allowances cover
with room to spare. Debug stays under its static path.

**On a target** the allowances change: there is no C library under the port, and the
asynchronous frame is the interrupt's, plus nesting. The static path is what carries over. There,
`uxTaskGetStackHighWaterMark` works, and would be the third check.

## Phase 2 report: the FreeRTOS shell, and the switch-over

**What changed.** The game runs as an actor, and the callback facade is gone:
- **`GameActor`** is pure: messages in, replies and events out to an outbox. It holds a
  pinsetter roll the game rejects, with every roll after it, until a correction or a discard lets
  them through, and counts what it loses.
- **The shell** (`rtos/`) gives it one static FreeRTOS task. The task is fed by:
  - a command queue;
  - the pinsetter's queue, filled from the interrupt side;
  - a one-slot report of rolls the interrupt lost to a full queue.

  It wakes on its task notification, because V11.1.0 has no static queue set. Every output
  goes, with no wait, to the queue the message named. An output its queue can't take is dropped
  and counted.
- **The legacy code is deleted:** kay-oo's `Game` facade, its listeners, `Scorecard` adapter,
  `RollLog` and slot pool, and the pinsetter's mailbox. Only tests called them. Their tests went
  with them, after every behavior was mapped to a test of the new code, or to the hazard that can
  no longer happen (above). 5 tests with no equivalent were ported first.

The phase 2 decisions are all in:
- **(a):** the newest roll is dropped on a full queue, counted, and reported as an event.
- **(b):** replies carry a sequence number and go to the caller's own queue, with no wait.
- **(c):** the actor keeps a held list, reports a "roll held" event, and replays the held rolls
  after a correction or discard.

**Metrics against the baseline** (kay-oo at ad857ff; now `7580df7`):

| | Baseline | Now |
|---|---|---|
| Tests | 114 (host) | 82 on the host, plus 8 on the POSIX port (Linux) |
| Functions (lizard) | 143 | 111, `rtos/` included |
| Average / maximum cyclomatic complexity | 1.6 / 5 | 1.7 / 9, `GameActor_Handle`'s dispatch (limit 10) |
| Maximum NLOC per function | 16 | 30, the same dispatch (limit 50) |
| Maximum parameters | 4 | 4 |
| Maximum cognitive complexity | 4 | 5, `GameShell_PinsetterCountedFromIsr` (limit 7) |
| Largest frame, release | 224, `Game_CorrectRoll` (GCC 16) | 256, `Scorer_Edit` (GCC 13) |
| Stack depth from each task entry | not measured | 3952 / 4224 bytes with host allowances, budget 4608 (the call graph) |
| Release line coverage | 99.8% of 583 | 99.6% of 545, `rtos/` included |
| Release branch coverage | 98.9% of 174 | 96.3% of 164 |
| Function pointers | 6 | 1, the game task's entry, which FreeRTOS requires |
| NLOC, `src/`, `include/` (and `rtos/`) | 1,283 | 1,079 |

The two lines uncovered in release are the scorer's unreachable `LANE_OVER` case, whose `assert`
compiles away under `NDEBUG`. The branches not taken are mostly the shell's: a queue that is
never full when a test sends to it, and the interrupt's yield when it wakes no one.

**Mutation feedback at the phase stop** (`tools/mutation.sh`, 4 workers): debug and release each
had 313 mutants, of which 302 were killed and 11 survived, a score of 96.5%. The runs took 6 m 57 s
and 7 m 44 s. Two survivors were real gaps, and coverage found a third. Each is pinned now, and
fails against its mutant:
- a held roll refused again, for another reason, reports the new one;
- `GameActor_Init` starts with nothing held;
- a subscriber joining mid-frame is caught up on the complete frames only.

The other nine can't be caught by a test:
- **Equivalent:** `fault.c`'s discarded `fputs` result and its trailing newline, two initial
  values always written before they're read, `NULL` versus the message's own array for no new
  balls, and two in the catch-up's loop bound.
- **Undefined behavior that only a sanitizer would catch:** a frame's `closed = false` on the
  uninitialized stack lane (MemorySanitizer, which isn't available here), and the held list's
  shift reading one past a full list (UBSan traps on it).

The edit property tests are left out of mutation runs, as the facade's were: under Mull's
instrumentation they take the test binary past its timeout.

**Surprises.**
1. **FreeRTOS V11.1.0 has no static queue set.** `xQueueCreateSet` needs the heap. The task
   notification does the same job, and is simpler: one wake-up for any number of queues.
2. **The painted stack was deeper than the call graph, in release:** 1583 bytes against the
   static path's 880. The difference is the C library under the POSIX port, and the port's
   signals, whose handlers run on the task's stack. The call graph alone would have undersized
   the host's stack, so the allowances are measured, and named in the contract.
3. **The only interrupt-side stack tripwire was legacy.** It was on `pinsetter_isr.c`, and
   would have gone with the pinsetter. So the shell's interrupt side moved to a file of its own,
   under its own limit, first.
4. **A comment had gone false.** `GAME_ERR_TOO_MANY_ROLLS` still said "more than 21 rolls" after
   candlepin made it 30. When the comments were sorted before the shell, about half turned out to
   be a name's job, and a fifth were claims a test or an assert now checks. One such claim, the
   outbox's capacity, was 2 too big.
5. **The facade's deletion was mostly deleting hazards.** About a third of its tests pinned what
   only callbacks and handles can get wrong: re-entry, destroying twice, a caller's impure
   counting rule. With no callbacks, those tests have nothing left to test.
6. **Process:** stopping a background gate run stopped its shell but not the scripts it had
   started. For a few minutes two runs built and ran the same test binary. Windows showed
   "application was unable to start" when one launched a half-written executable. No result was
   affected, but the stray processes were killed by hand, and are now checked for before every
   gate run.

**Open for phase 3:**
- The README, `ARCHITECTURE.md` and `STATE_PATTERN.md` still describe kay-oo, and are to be
  rewritten.
- `GameMessage` and `GameOutput` carry every kind's fields, with comments saying which kind uses
  which. A tagged layout would say it in the type, and would shrink what each queue copies.
- The target stage (the QEMU plan above) is still deferred.

**Stop.** Phase 2 ends here, as the brief asks.

## Phase 3: polymorphism by id, and late binding through messages

### Interim report, after step 4: the rules arrive in a message

**What changed**, in the brief's order:

1. **An envelope and a tagged payload.** Every message is an `Envelope` (selector, from, to, seq)
   and a payload union holding only its selector's fields.
2. **Actor ids and the routing table.** Senders and the game address `ActorId`s, one byte each.
   The shell's routing table binds each id to a kind and a mailbox, and no queue handle crosses
   into `src/`. The game actor holds no pointers at all. An output to an unbound id, or to one
   past the table, is dropped and counted. Before, the first stopped the program in FreeRTOS's
   `configASSERT`, and the second read past the table.
3. **Delivery by kind.** Requests, replies and events share one protocol, `include/message.h`:
   - Sending posts to the mailbox at the message's `to`, a table lookup with no switch.
   - The shell's task then dispatches on the `ActorKind` bound there, in
     `GameShell_Dispatch`'s switch. That's the single late-binding point, and every target is a
     direct call the call graph sees.
   - A selector a kind doesn't answer gets `MSG_NOT_UNDERSTOOD` back, with its seq, and is
     counted. A NOT_UNDERSTOOD itself, or a message from no one, is counted but never
     answered, or two kinds that don't understand each other would echo forever.
   - What the game understands is data too: a designated-initializer table maps each selector to
     the game's own request enum, and a selector it doesn't list reads as "doesn't understand".
4. **The rules as data, in `NEW_GAME`:**
   - The scorer plays whatever `ScorerRules` it is started with: frames, balls per frame, pins
     per rack, bonus balls by clearing ball, and, as you decided, the count rule as one field
     (pins still standing, off a full rack, that count as a clear). `ScorerVariant`, `CountRule`
     and the scorer's compiled-in table are gone.
   - The ten-pin, no-tap and candlepin presets live with the senders, in `test/rules_presets.h`.
   - The game starts with no game. Before one, anything but a NEW_GAME is answered "no game",
     except the pinsetter's rolls, which are held for the first game. A NEW_GAME mid-game is
     refused.
   - After a game, a NEW_GAME starts the next. The rolls held in the meantime are played into
     it, as you decided, and subscribers hear the old game's frames reopened.

**The hypothesis so far.**

| Claim | Evidence | So far |
|---|---|---|
| Once the rules arrive in a message, a variant needs no code | A 5-frame game, and a 3-ball game at a rack of 5, were never compiled in. Both pass 5000 random games each, checked after every ball against a new reference sharing no code with the scorer, with the scorer untouched. The reference agrees with the scorer on ten-pin and candlepin, which two other references check. | **Held** |
| The lifecycle becomes an explicit state machine because the feature forces it | Before NEW_GAME, the actor played on a scorer that was never started. `GameLifecycle` (`GAME_AWAITING_RULES`, `GAME_IN_PLAY`) is what NEW_GAME forced. "Over" stayed the scorer's to say: an edit after the last ball can reopen a game, and a copy in the state would go stale. | **Held**, with two states, not three |
| The actor core holds no pointers, and no queue handle crosses into `src/` | `GameActor` is ids, counts and a `Scorer` value. The shell owns every `QueueHandle_t`. | **Held** |
| The tagged layout shrinks what each queue copies | It did, until the protocol was unified (below). | **Partly failed**; see the table |

**What each queue copies**, in bytes, on both toolchains:

| | Phase 2 | Tagged (step 1) | Ids (step 2) | One protocol (step 3) |
|---|---|---|---|---|
| A request (`GameMessage`, then `Message`) | 56 | 56 | 44 | 44 |
| A reply or event (`GameOutput`, then `Message`) | 40 | 32 | 20 | **44** |
| The game's outbox | 1728 | 1384 | 864 | **1896** |

- **Tagging didn't shrink the request.** The edit's payload is its 30 new balls inline, so a
  queue can copy it, and a union is as big as its largest member.
- **Dropping the pointer did most of the shrinking.** It took 12 bytes off a request and 12 off
  an output.
- **One message type for every kind then cost the outputs.** A reply or an event is a
  `Message`, sized by the edit it will never carry: 24 bytes more per copy than step 2. That's
  the price of any kind being sendable any selector. The rules in NEW_GAME cost nothing: their
  payload is 7 bytes. So the hypothesis is refuted for outputs.
- **To win the outputs back,** an edit's inline balls would have to shrink, for example a cap
  on how many balls one edit replaces. That would be a behavior change, and it's yours to
  decide.

**What this lost so far.**
- **The rules' `_Static_assert`s are now runtime validation.** `Scorer_Start` refuses rules
  with:
  - more balls than it holds, no frames or more than ten, or no balls a frame or more than
    three;
  - a bonus for a ball past the frame's own;
  - a rack of no pins or more than 20, or a clear that every ball makes.

  Two of those were real hazards found by the tests: 11 frames within the ball limit walked
  past the lane's frames, and 0 frames wrapped the longest-game sum round to 1. The one static
  assert left bounds the scorer's own limits: 10 frames × 30 balls × 20 pins fits a `Score`.
- **The variants were a closed set checked at compile time. The rules are now open,** so they
  have a hazard surface: rules the scorer can't play (refused), messages before a game
  (answered), NEW_GAMEs mid-game (refused), and selectors a kind doesn't understand
  (NOT_UNDERSTOOD).
- **A caller-supplied counting rule is still lost,** as phase 1 recorded. The count rule is one
  field, and doesn't cover kay-oo's one-pin-left rule.

**Recorded disagreements with the constitution.**
- **Duplicate Switch Case.** Each kind switches on the protocol's selector, and the
  constitution's smell catalog calls the same discriminant switched in several places Duplicate
  Switch Case. Its remedy is a function-pointer table. Here Power of Ten wins: the selector
  switch is per kind by design, and the one switch on the kind is the shell's.
- **ENG-3.1's complexity limit shaped the code.** The game's request switch is at cyclomatic
  complexity 10, the limit. NEW_GAME, the lifecycle's own message, is taken before the switch,
  where one more `case` would have gone to 11. What the game understands became a table for
  the same reason.

**Metrics** (phase 2 → now):

| | Phase 2 | Now |
|---|---|---|
| Tests, host / with the POSIX port | 82 / 90 | 101 / 111 |
| Largest cyclomatic complexity | 9, `GameActor_Handle` | 10, `GameActor_Receive` (limit 10); `GameActor_Handle` 4 |
| Stack contract, game task, release / debug | 3952 / 4224 | 3936 / 4320 (budget 4608) |
| Release line coverage | 99.6% | 99.3% (99.0% before the two tests below) |

The misses:
- **The shell's dispatch of a message to a kind it doesn't run.** It's unreachable, because a
  message is only posted to the mailbox at its own `to`.
- **The scorer's unreachable lane phase.**
- **Two game paths:** a selector past the protocol's end, and a lost-roll report before any
  game. Both are now tested (below), which brings line coverage back up.

**Mutation feedback at this stop:** debug had 402 mutants, of which 384 were killed and 18 survived, a score of 95.5%; release had
399, of which 386 were killed and 13 survived, 96.7%. The runs took 11 m 13 s and 10 m 34 s. The
debug run was taken before the fixes below, and the release run after them. Of the debug
survivors:
- **Nine are phase 2's known classes:** equivalent mutants, and undefined behavior only a
  sanitizer catches.
- **Two new ones are equivalent:** the reopening loop's bound, which `Scorer_Frame` answers
  past the frames started, and a `>` for a `>=` when taking a maximum.
- **Two were dead code, now deleted:** the "no game" reason recorded for the first roll held
  before a game. Nobody can subscribe before a game to hear it, and the NEW_GAME's replay
  records a fresh reason if it refuses one.
- **Four were real gaps, now pinned by tests** that fail against their mutants:
  - a selector exactly at the protocol's end. Its read one past the game's table only traps
    under UBSan; elsewhere the table's neighbour happens to agree, so this mutant still
    survives the plain builds;
  - the pinsetter's lost count from before any game;
  - the last ball's own fill balls in the longest-game sum;
  - a rack of exactly the limit, 20 pins, being accepted.
- **One is left unpinned on purpose:** an event's `seq`, set to 0 so an event carries no stale
  bytes. The protocol gives events no seq to check.

The 5000-game reference runs are left out of mutation runs, like the edit properties, because
they take the binary past Mull's timeout.

**Decisions still to bring you**, from the brief, at steps 5 and after:
- one actor per task versus several sharing a task, once the cost of a task per actor is
  measured;
- the full-queue policy for the new kinds.

Also, the output size above: whether to cap an edit's inline balls.

**Stop.** Step 4 ends here, for your review.
