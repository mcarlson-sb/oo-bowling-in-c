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

### Decisions after the interim review (2026-09-29)

- **An edit's inline balls stay uncapped.** The 24 bytes more per reply or event are the price
  of one protocol. Revisit only if a target's RAM forces it.
- **One actor per task, to start.** Step 5 measures what a task costs (its static TCB, its
  stack, its mailbox's storage) before anything changes that.
- **The new kinds drop the newest on a full queue, and count it,** as the game does.
- **Before any new kind, the shell becomes an actor host.** A route binds an id to a kind, an
  instance and a mailbox, and the one dispatch switch reaches any kind's receive function.
- **Each kind gets its own selector table,** named as that kind's protocol (Smalltalk's
  `respondsTo:`). If a kind doesn't need one, the report says so, and says whether the game's
  table earned its keep or only moved a branch out of the complexity count.

### Step 5: more kinds at subscriber ids, and what a task per actor costs

The shell is an actor host now. A route binds an id to a kind, an instance and a mailbox, and
the one dispatch switch reaches any kind's receive function. Two kinds joined the game:
- **A scoreboard,** which rebuilds the frames from FRAME_CHANGED events and answers
  QUERY_SCORE with their total.
- **A running average,** kay-oo's no-tap average listener, now an actor. It hears the same
  events, and answers the same QUERY_SCORE with their average.

Both keep a `FrameBoard`, a plain value. Each kind has its own protocol table (`respondsTo:`),
and answers NOT_UNDERSTOOD through the shared outbox.

**The rebinding proof.** One scenario, in the same client code, subscribes an id to the same game,
rolls a spare and an open frame (7, then 12), and asks whoever sits at that id QUERY_SCORE:

| Bound at the id | Answer |
|---|---|
| a scoreboard | 19, the total |
| a running average | 9, the average, rounded down |
| a recording double (an external queue) | none; it records the subscription's reply, both events and the question |

Neither the game's code nor the sender's changes between the three. With the two hosted kinds'
dispatch cases swapped, both hosted bindings fail. The binding is made at startup, before the
scheduler runs. That way the routing table is written only while nothing else can read it, and
needs no lock.

**What one hosted actor's task costs**, measured on this host (x86-64, the POSIX port, GCC 13):

| Part | Bytes | Note |
|---|---|---|
| Stack | 16384 | The port's minimum, `PTHREAD_STACK_MIN`; the contract needs at most 4320 |
| Outbox | 1896 | 43 messages: the game's worst case, and every hosted task has one |
| Mailbox storage | 176 | 4 messages of 44 |
| Queue control block (`StaticQueue_t`) | 144 | |
| Task control block (`StaticTask_t`) | 128 | |
| The message being handled | 44 | |
| Painted-stack record | 16 | Host only |
| **One hosted task** | **about 18.8 KB** | |
| The actor's own state | 34 (scoreboard, average), 96 (game) | |

The shell's static RAM, with three actors hosted, is 57 KB.

**Stack depth by kind**, from the call graph, without the host's allowances:

| Entry | Release | Debug |
|---|---|---|
| `GameActor_Handle` | 800 | 1280 |
| `Scoreboard_Handle` | 48 | 224 |
| `RunningAverage_Handle` | 64 | 224 |
| `GameShell_Task`, every hosted task's entry | 896 | 1392 |

What this shows:
- **The one dispatch switch has a stack cost.** Every hosted task runs the same entry, which
  reaches every kind, so the call graph can't know which kind a task hosts. Every task's
  contract is the deepest kind's, the game's, while a scoreboard's own code needs a sixth of it.
  Only the dispatch switch could fix this: a task entry per kind would be a second switch on the
  kind, or a function pointer.
- **Most of a task's cost is hosting, not the actor.** On a target, each task would still carry
  a stack sized for the game (about 1.3 KB of code path, plus the interrupt frame), the outbox,
  the queue storage and the control blocks: roughly 3.5 to 4 KB, for an actor whose state is 34
  bytes. The outbox is the biggest part after the stack. It is sized for the game's worst case,
  though an observer sends at most one reply per message.
- **These are the numbers for the decision** between one actor per task and several sharing a
  task. They are yours to make, as decided. Nothing is changed yet.

## Phase 3 report: polymorphism by id, and late binding through messages

**What changed.** Kay's three properties, as far as Power of Ten lets them go:

1. **One protocol** (`include/message.h`). Every message, request, reply or event, is an
   envelope (selector, from, to, seq) and a payload of its selector's fields only. Replies and
   NOT_UNDERSTOOD are answered the same way by every kind (`src/outbox.c`).
2. **Actor ids and an actor host.** Senders address `ActorId`s, one byte each:
   - the shell's routing table binds each id to a kind, an instance and a mailbox;
   - each hosted actor has a task of its own;
   - the one switch on the kind at a message's `to` calls that kind's receive function.

   No queue handle, and no pointer, is part of any actor's state.
3. **The rules as data.** NEW_GAME carries them: frames, balls a frame, pins a rack, bonus balls
   by clearing ball, and the count rule, as one field. The scorer compiles no variant in: the
   presets live with the senders. Rules it can't play are refused. The game's lifecycle is
   explicit, and forced by the feature: awaiting rules, or in play.
4. **More kinds.** A scoreboard, and a running average (kay-oo's no-tap average listener). Both
   rebuild the frames from FRAME_CHANGED events in a `FrameBoard`, and answer the same
   QUERY_SCORE the game does, each its own way. Each kind has its own protocol table,
   `respondsTo:`.
5. **Hidden state.** Each actor's struct is defined in `src/*_state.h`. The public headers have
   an incomplete type, and a test fails if a definition moves back.
6. **The docs.** The README, `ARCHITECTURE.md` and `STATE_PATTERN.md` describe the code as it is.

**The hypothesis, tested.**

| Claim | Evidence | Held? |
|---|---|---|
| Rebinding an id changes behavior without the sender changing | One scenario, in the same client code, against the same game. At the subscriber's id, a scoreboard answers 19, a running average 9, and a recording double records the game's messages and answers no one. With the two hosted kinds' dispatch cases swapped, both hosted bindings fail | **Held.** The binding is made at startup; rebinding a live id is not built (see decisions) |
| Once the rules arrive in a message, a variant needs no code | A 5-frame game, and a 3-ball game at a rack of 5, never compiled in. Both play 5000 random games against a reference that shares no code with the scorer, with the scorer untouched | **Held** |
| The lifecycle becomes an explicit state machine because the feature forces it | Before NEW_GAME, the actor played on a scorer that was never started. Two states were forced, not three: "over" stays the scorer's to say | **Held** |
| The actor core holds no pointers, and no queue handle crosses into `src/` | The game, the scoreboard and the average hold ids, counts and values. The shell owns every `QueueHandle_t`. The one pointer in `src/`'s API, `RollEdit.new_pins`, is transient: it points into the message being handled, and is never kept | **Held** |
| The tagged layout shrinks what each queue copies | A request went from 56 bytes to 44, most of that from the ids replacing a pointer. A reply or event went from 40 to 44: one type for every kind is sized by the edit's 30 inline balls | **Failed for replies and events**, and kept on your decision |
| Actor state can be hidden from everything but the shell and the tests | Enforced by the build: only three targets have `src/` on their include path, and `actor_state_is_hidden` fails if a definition becomes visible from `include/`. Within those targets it is still convention, since the tests read the actors' counters directly | **Held, by the include path, not the language** |

**The protocol tables: earning their keep, or moving a branch?**
- **The observers' tables don't earn it on complexity.** The scoreboard and the running average
  answer three selectors each. A switch with a `default:` would be as clear, and as short.
- **What the table buys every kind is two properties.** A selector the protocol gains later
  reads as "doesn't understand", by construction, until someone lists it. And the switch on the
  kind's own request enum keeps `-Wswitch`'s check, which a `default:` would switch off.
- **The game's table mostly moved branches out of the count.** Its request switch sits at the
  complexity limit, 10. A switch on the selector itself would need a case for every selector the
  game doesn't answer as well, or a `default:`. The table took those cases out of the count,
  while keeping the check.
- **So:** the table is a real design element for the "unlisted means not understood" rule, and
  partly a way around the complexity count. It's kept for both, and recorded as both.

**NEW_GAME is decided outside the lifecycle switch.**
- It is the lifecycle's own message: the one that moves the state, so it comes before the state
  is consulted.
- Whether a game is still in play is the scorer's to say, and a third lifecycle state holding
  "over" could go stale when an edit reopens a finished game.
- As one more case of the request switch, it would have taken that switch past ENG-3.1's
  limit.

**Metrics** (the baseline is kay-oo at ad857ff; phase 2 is at 194fb52):

| | Baseline | Phase 2 | Phase 3 |
|---|---|---|---|
| Tests, host / with the POSIX port | 114 / – | 82 / 90 | 115 / 128 |
| Functions (lizard) | 143 | 111 | 149 |
| NLOC, `src/`, `include/`, `rtos/` | 1,283 | 1,079 | 1,602 |
| Highest cyclomatic complexity (limit 10) | 5 | 9 | 10, `GameActor_Receive` |
| Highest cognitive complexity (limit 7) | 4 | 5 | 5 |
| Function pointers | 6 | 1 | 1, the task entry |
| Release line coverage | 99.8% | 99.6% | 99.1% |
| Stack contract, game task, release / debug (budget 4608) | – | 3952 / 4224 | 3968 / 4320 |
| A request / a reply or event, bytes | – | 56 / 40 | 44 / 44 |
| One hosted task, this host | – | – | about 18.8 KB; about 2.4 KB of it besides the stack |
| Actor state: game / scoreboard / average, bytes | – | 104 (game) | 96 / 34 / 34 |

Phase 3 took 51 commits, this report's included: 23 `[make-change]`, 9 `[make-easy]` and 19
`[clean-up]`.

**What the change lost.**
- **The rules' `_Static_assert`s became runtime validation.** What was a closed set checked at
  compile time is now open, with a hazard surface of its own:
  - rules the scorer can't play, which are refused;
  - messages before a game, which are answered;
  - a NEW_GAME mid-game, which is refused;
  - selectors a kind doesn't respond to, which get NOT_UNDERSTOOD.

  The tests for the refusals found two real hazards: 11 frames within the ball limit walked
  past the lane's frames, and 0 frames wrapped the longest-game sum round to 1.
- **Replies and events copy 24 bytes more,** for one protocol, kept on your decision.
- **Every hosted task is sized for the deepest kind.** The one dispatch switch, reached from one
  task entry, keeps every call target visible. But the call graph can't know which kind a task
  hosts, so a scoreboard's task gets the game's stack, while its own path is 48 bytes in
  release.
- **The kinds are a closed set.** A new kind is a new case in the shell's switch and a new
  instance array: a code change in the shell. Kay's late binding would add a kind at run time;
  Power of Ten, with no function pointers, can't.
- **The Duplicate Switch Case.** Each kind switches on the protocol's selector, which the
  constitution's smell catalog flags. Its remedy is a function-pointer table.
- **A caller-supplied counting rule stays lost.** The count rule is one field, and doesn't
  express kay-oo's one-pin-left rule.

**Surprises.**
1. **One protocol cost the outputs** what tagging had saved them. A union is as big as its
   largest member, and every kind's messages share it.
2. **Validating the rules found real bugs,** the hazards above. Neither had been reachable while
   the rules were compiled in.
3. **The complexity limit shaped the design twice:** the game's protocol became a table, and
   NEW_GAME was taken before the lifecycle switch. Both were recorded rather than hidden.
4. **Hosting costs about 70 times the actor.** A scoreboard is 34 bytes of state, and about 2.4
   KB of hosting even before its stack. Most of that is an outbox sized for the game's worst
   case, and an observer sends at most one reply per message.
5. **A test that waits for what can't come waits out its timeout.** The recording double's
   first version waited a second for a reply that no one sends. Under ThreadSanitizer the
   POSIX port's ticks slowed that second past the test's 60-second limit. The test now waits for
   what does arrive.
6. **The hidden-state probe needed each compiler's words.** GCC and clang refuse an incomplete
   type differently, and GCC quotes with curly quotes in a UTF-8 locale. The probe runs in the C
   locale, and has a pattern for each compiler.
7. **Process:** the local every-commit check compared against a local `rtos-actor` branch that
   never moves, since promotion moves the remote one. It re-checked every commit since phase 2,
   until it was pointed at the promoted commit.

**Mutation feedback at the phase stop:** debug had 450 mutants, of which 424 were killed and 26 survived, a score of 94.2%, in 11 m 54 s,
before the tests below. Release had 450, of which 431 were killed and 19 survived, 95.8%, in
15 m 34 s, after the first of them. The survivors:
- **The classes known from phase 2 and the interim stop:** equivalents, the event's
  deliberately unpinned `seq`, and undefined behavior that only UBSan catches (including the
  selector exactly at the protocol's end).
- **The frame board:**
  - its index arithmetic and its loops' bounds, which read one past its arrays: undefined
    behavior, trapped only by UBSan;
  - the clearing of its scores, which is equivalent, since a score is only read once its frame
    is complete, and a frame only becomes complete by being heard, which writes its score;
  - a Mull artifact: storing 42 in a `bool` reads back as false under clang, which loads a
    `bool` by its lowest bit, so that mutant leaves the flag cleared. My own mutant, which leaves
    the memory as it was, is killed.
- **Real gaps, now pinned by tests** that fail against their mutants:
  - a scoreboard and a running average start empty whatever memory they are given (6 mutants:
    every test had started them in memory that happened to be zero);
  - the frame board keeps the last frame, 10;
  - each observer says NOT_UNDERSTOOD from its own id (2 mutants).
- **A slip in my own proof:** a hand mutant that didn't compile looked like a survivor, because
  the harness read only the test results. Rerun as a mutant that compiles, it was killed.

**Decisions for phase 4.**
- **One actor per task, or several sharing a task.** A hosted task costs about 18.8 KB on this
  host, and about 3.5 to 4 KB on a target, for 34 bytes of observer state. Sharing a task would
  share the stack and the outbox. It would still need one mailbox per actor, or one per task,
  with the dispatch choosing the actor by `to`, as it does now.
- **An outbox per kind.** Sized for each kind's worst case, it would cut about 1.8 KB from each
  observer's task. The capacity would become a property of the kind, and the shell's hosted slot
  would need a size per kind.
- **A stack contract per kind.** Only possible with an entry per kind, which is a second switch
  on the kind, or a function pointer. So far the one switch has been worth the cost.
- **Rebinding a live id.** The routing table is written only before the scheduler starts, so it
  needs no lock. Rebinding at run time would need a critical section around every lookup, or a
  message to the shell that rebinds between dispatches.
- **Counters as queries.** The tests read the actors' counters (not understood, lost) from their
  state. A statistics selector would let the tests ask instead, and would make the hidden state
  hidden from the tests too.
- **The target stage** (the QEMU plan in the gated-CI section) is still deferred. A target would
  make the stack and RAM figures real ones, where these are the host's.

### Kay's three properties, scored

| Property | Enforced by the structure | Still convention | Overridden by Power of Ten |
|---|---|---|---|
| **Messaging** | An actor's public interface is its init and its receive function; everything else is a message. Actors in different tasks meet only through queues. A selector a kind doesn't respond to is answered NOT_UNDERSTOOD, never dropped | The tests call a kind's receive function directly, as the shell does: they stand in for the host | The scorer is called, not messaged: a value inside the game, by design, since it has no lifetime or concurrency of its own |
| **Local, protected state** | Each actor's struct is in `src/`, off every include path but the library's, the shell's and the tests'. `actor_state_is_hidden` fails if that changes | Inside those three targets, nothing stops code reading an actor's fields, and the tests do read its counters | Static allocation means the host must know each actor's size to allocate it, so the state can't be hidden from the host. With a heap, an opaque handle could hide it from the host too, and the heap is ruled out |
| **Extreme late binding** | Who receives a message is the routing table's data, and what it means is the bound kind's. The rules are a message. The same QUERY_SCORE gets three answers from three kinds, and the game's code doesn't know which | Bindings are made at startup only. The routing table isn't locked, so it must not change while tasks run | The kinds are a closed enum, and the dispatch a switch over it: a new kind is a code change in the shell. Each task's stack is sized for the deepest kind, because one entry reaches them all. Both are the price of every call target being known |

**Stop.** Phase 3 ends here, as the brief asks.

## Phase 4: actors are not tasks, and two lanes

### Decisions from the phase 3 review (2026-09-29)

1. **Shared tasks, split by role.** Actors are not tasks, as with F Prime's active and queued
   components. A route binds an id to a kind, an instance, a mailbox and a hosting task. Each
   game gets a task of its own; the observers share one, dispatched by `to` as now. The RAM is
   measured again after the change.
2. **The outbox belongs to the task, not the kind.** Each task has one, sized for the largest
   reply burst of the kinds it hosts. An observers-only task doesn't carry the game's
   43-message outbox.
3. **No stack budget per kind.** Sharing a task pays the game-sized stack once. The fallback, if
   a target's RAM forces it, is a second switch per task role (game tasks, observer tasks),
   never per kind. It is recorded here, not built.
4. **No live rebinding.** Deferred, because sends look the routing table up in the sender's
   task, so a rebind message to the shell wouldn't remove the race. It would need every send
   routed through one task, or an atomic update of the table.
5. **Counters become queries:** a statistics selector every kind answers. The tests ask for it,
   instead of reading an actor's state.
6. **The QEMU target stage becomes phase 5.** The phase 4 report marks which conclusions hold
   on this host only, and would change on a target.

**The driving feature: two lanes.** Two game actors, one playing ten-pin and one candlepin, each
started by its own NEW_GAME, with a scoreboard and a running average subscribed to both. It's
the first time a kind has more than one instance.

**What phase 4 should show, said before it is tested:**
- **Sharing a task changes the shell only.** No actor's code changes when the observers move
  into one task.
- **An observer costs far less than a task.** An observer in the shared task costs its state and
  its route: no stack, no task control block, no queue and no outbox of its own. So the second
  observer's cost falls from about 18.8 KB to its 34 bytes.
- **A second game needs no game code.** A game is an instance, and two lanes are two routes.
- **Expected to fail: an observer of two games.** A FRAME_CHANGED doesn't say which game sent
  it, except through its envelope's `from`, and a `FrameBoard` is keyed by frame number. So a
  scoreboard subscribed to both lanes should mix their frames, and the observers will have to
  change.
- **The statistics selector hides state from the tests too.** They will still need the state
  headers to allocate an actor, but no longer to read one.

### Interim report, after the task/actor split

**What changed.** Actors are not tasks now:
- **A hosting task** is one mailbox for every actor it hosts, the message it's handling, and
  one outbox sized for its role.
- **A route** binds an id to a kind, an instance, and the mailbox and task of the task that
  hosts it.
- **The game** has a task of its own, whose outbox holds 43 messages. **Every observer** shares
  the observers' task, whose outbox holds 1. That's static-asserted against each observer
  kind's declared largest burst (`SCOREBOARD_MOST_SENT`, `RUNNING_AVERAGE_MOST_SENT`), which
  their tests hold them to.
- **The outbox is the task's storage.** An `Outbox` is its items, in storage its owner provides,
  and its capacity.

**What it should have shown, tested:**

| Prediction | Evidence | Held? |
|---|---|---|
| Sharing a task changes the shell only | `src/`'s actors are unchanged: the diff of the split touches `rtos/`, the outbox's storage, and each observer's declared burst. The rebinding tests pass unchanged, a scoreboard and an average now sharing a task | **Held** |
| An observer costs far less than a task | The shell's static RAM, hosting a game, a scoreboard and a running average, fell from 57,184 bytes to 36,552. A second scoreboard adds 32 bytes: its state, and nothing of a task's | **Held** |

**RAM, before and after** (this host, x86-64, the POSIX port, GCC 13):

| | Before: a task each | After: the observers share one |
|---|---|---|
| The shell's static RAM (`s_shell`), a game and two observers | 57,184 | 36,552 |
| Each further observer | about 18.8 KB, a task of its own | 32 bytes, its state |
| A game's task, besides its stack | about 2.4 KB | about 2.4 KB (43-message outbox) |
| The observers' task, besides its stack | – | about 0.6 KB (1-message outbox) |

**Host only; this would change on a target.**
- **The stack size.** Every task's 16 KB stack is the POSIX port's `PTHREAD_STACK_MIN`, not the
  contract. The contract needs 4320 bytes at most, and on a target, without the host's
  allowances, about 1.4 KB. So on a target the split saves less in absolute terms, about 3.5 KB
  per observer instead of 18.8 KB, but still the same fraction.
- **The control blocks** (`StaticTask_t` 128, `StaticQueue_t` 144) are this port's. A target's
  are smaller.

**Recorded, not built (your answers 3 and 4):**
- **A stack per task role,** if a target's RAM forces it: a second switch per role (game tasks,
  observer tasks), never per kind.
- **Live rebinding is deferred.** Sends look the routing table up in the sender's task, so a
  rebind message to the shell wouldn't remove the race. It would need every send routed through
  one task, or an atomic update of the table.

**Process note.** A local WSL build twice ignored a source file edited and restored within a
second or two from Windows. It kept the old object, so a size measurement read stale. The two
clocks agree, so it looks like equal timestamps across the file bridge. CI and the every-commit
clone build from scratch, so neither is affected. Measurements now delete the object first.

**Stop.** The task/actor split ends here, for your review. Next: the two lanes, and the
statistics selector.

### Settled before the lanes: the observers outrank every game

**The choice.** The observers share one mailbox of 4, and a game sends up to 43 messages for one
of its own, with no wait. There were two options:
- **Observers below the games.** A mailbox sized for one message's burst doesn't bound it: a
  game drains everything waiting before it blocks, up to 32 pinsetter rolls, a lost report and 4
  commands. So the observers' backlog is bounded only by a whole busy period. That means
  kilobytes of mailbox, or counted drops.
- **Observers above every game, which is what's built.** The observers' task preempts a game
  after each post, so the mailbox holds one of its events at a time.

**The cost is deadline order.** An observer's work delays the game, and the game drains the
pinsetter. It's bounded only because an observer's work per event is a few frames' arithmetic.

**So a condition goes on every observer kind:** its handling must be short and bounded, or it
doesn't go in the observers' task. A future observer that does real I/O, such as writing a UART,
a log or a display, breaks the bound, and belongs in a task of its own below the games, with
its mailbox sized for what it may fall behind by. Phase 5 has something concrete to measure on
the target: an observer's worst-case handling time against the game's latency budget.

**Enforced, and tested:**
- **The check.** `GameShell_Start` stops, with the reason, unless the observers' priority is
  above the game's. With two lanes it checks every game's task.
- **The death test,** which fails if the check goes.
- **The burst test.** The game's worst burst, 21 events to each of two hosted observers,
  drops nothing. With the observers below the game, 74 are dropped.

**Host only:** on the POSIX port, preemption is simulated with signals, so "nothing dropped" is
good evidence on this host, not proof. Phase 5 checks it on the target.

**Found by the burst test.** Its first run dropped 22 messages with the observers above the
game. The 13 held pinsetter rolls reached both observers as ROLL_HELD events, and neither
listed that selector, so they answered 26 NOT_UNDERSTOODs into the game's own mailbox of 4,
while the game was still busy. So every subscriber is sent the whole subscriber protocol. The
observers now list ROLL_HELD and ROLLS_LOST, next to the subscription's reply, as heard, with
nothing to do. Answering an event with NOT_UNDERSTOOD turns a burst into a burst the other
way.

**A correction to the interim report:** "each further observer costs 32 bytes" holds up to the
compile-time count of each kind (`GAME_SHELL_SCOREBOARDS`, `GAME_SHELL_RUNNING_AVERAGES`).
Beyond it, hosting another is refused. Raising the count costs each extra instance's state,
statically, whether it's used or not.

### A hang in the kernel's POSIX port, and FreeRTOS V11.2.0

A gate run for phase 4's two lanes hung in `build-debug`, `build-release` and `every-commit`,
and was cancelled.

**What it was.** Pinned to one CPU, 6 to 8 of the 20 RTOS tests hung in every run, and different
ones each time, on any commit. That includes `0dad2b9`, which CI had promoted. A backtrace (gdb,
in a run started under it) showed where:

`vTaskEndScheduler → vTaskDelete → vPortCancelThread → event_signal → pthread_mutex_lock`

In V11.1.0's POSIX port, `vPortCancelThread` cancels a task's thread while it waits in
`pthread_cond_wait` on its event. A thread cancelled there takes the mutex back as it goes, and
the port has no cleanup handler to release it. So the thread exits holding the mutex, and
`event_signal` then waits on it forever.

It's a race. Many cores make it rare; one core makes it all but certain. Phase 4 made it likelier
by adding tasks, the observers' task and a second lane, since each is another thread to cancel.

**The fix.** V11.2.0 makes each event's mutex robust (`PTHREAD_MUTEX_ROBUST`), and handles
`EOWNERDEAD` when locking it, so the next lock recovers the mutex instead of blocking.
- **Verified:** four runs of all 20 RTOS tests on one CPU pass, where V11.1.0 hung 6 to 8 each
  time. The full suite passes.
- **Stale objects, once more:** a build directory that had built V11.1.0 kept its kernel objects
  after the upgrade. The fetched sources carry file times older than those objects, so ninja saw
  nothing to rebuild, and the one-CPU check still hung, on V11.1.0's code. A clean build of the
  kernel passes. CI always builds from nothing.
- **The regression check:** CI's `build-debug` now also runs the RTOS tests pinned to one CPU,
  and fails if the race returns.
- **Every ctest run in CI now has a timeout per test,** so a hang fails in minutes, not after
  ctest's default of 1500 s.

**The recovery,** as CLAUDE.md prescribes: `every-commit` tests every commit since the last
promotion, and each of them carried V11.1.0. So the line was rewritten. It was reset to the
promoted `0dad2b9`, the upgrade was committed there, the phase 4 commits after it were
cherry-picked on top, and the result was pushed with `--force-with-lease` against the red tip.

**Host only:** this was the POSIX port's own shutdown, which a target doesn't run. But a test that
ends the scheduler exercises it every time, which is why it surfaced here.

### The design review, applied before the stop

A review scored the code on Kay, SOLID, coupling, cohesion, GRASP, complexity and readability,
and named three changes that would lift the scores most. All three are made, with a fourth that
the third implied:

1. **The protocol no longer depends on the scorer** (D, and the coupling hub). `message.h`
   included `scorer.h` for `ScorerRules`, `FrameEvent` and the limits, so the observers pulled in
   the scorer through the protocol. They are in `include/rules.h` now, which the scorer, the
   protocol and the frame board include. A compile probe, `protocol_is_free_of_the_scorer`, fails
   if the scorer's type is visible through the protocol's or the observers' headers. It failed
   before the move.
2. **The held rolls and the subscribers are values** (cohesion), in `src/held_rolls.{h,c}` and
   `src/subscribers.{h,c}`, with accessors, as `FrameBoard` is. `game_actor.c` went from 449
   lines to 385. What it doesn't do: `GameActor_Receive` is still at complexity 10, since its
   switch is unchanged.
3. **QUERY_SCORE is QUERY_FIGURE, with its contract on the selector** (Kay against LSP, decided
   rather than left to drift). The three answers stay: they are the experiment's clearest result.
   What was missing was the contract a client substitutes against, and substitutability is judged
   against what the protocol promises, not against what "score" suggests. It now promises that
   each kind answers with the one figure it reports, and that the figure means what the kind
   says, as every Smalltalk object answers `printString` its own way. "Score" promised a total,
   which made an average look like a broken substitution; "figure" promises only what the
   contract does.
4. **Every kind answers QUERY_STATS, in any state.** The typed facts are where LSP is served, so
   a client must always be able to ask for them without knowing what is bound at an id. A
   protocol test asks each kind, freshly started. It found a gap: a game awaiting its rules
   answered "no game". It answers with its counters now. The lifecycle test that said "anything
   but a new game" gets "no game" was renamed to include the stats query; its assertions are
   unchanged.

The review's interface-segregation point is left alone, as it advised. One `Message` union is the
price of one protocol, and that price was chosen in phase 3.

## Phase 4 report: actors are not tasks, and two lanes

**What changed.**
1. **Actors are not tasks.** A route binds an id to a kind, an instance, a mailbox and a hosting
   task. Each game has a task of its own, and every observer shares the observers' task,
   dispatched by `to` as before.
2. **The outbox belongs to the task,** sized for the largest burst of the kinds it hosts: 43
   messages for a game's task, 1 for the observers'. Each observer kind declares its largest
   burst, and the shell static-asserts the observers' outbox against it.
3. **The observers outrank every game,** and `GameShell_Start` stops with the reason if they
   don't. The condition that goes with it: an observer kind's handling must be short and bounded.
4. **Two lanes.** A second game at an id of its own, in a task of its own, playing its own rules,
   and a scoreboard and a running average per lane. The routing table went from 8 routes to 16.
5. **Counters are queries.** QUERY_STATS, which every kind answers with its counters and the facts
   behind its answers. The tests ask for them instead of reading an actor's state.
6. **FreeRTOS V11.2.0,** for a deadlock in V11.1.0's POSIX port, with a one-CPU regression check
   and a per-test timeout in CI.
7. **The design review's changes,** in the section above.

**The hypotheses, tested.** Each was said before its test was written (see the decisions from the
phase 3 review):

| Prediction | Evidence | Held? |
|---|---|---|
| Sharing a task changes the shell only | The split's diff touches `rtos/`, the outbox's storage and each observer's declared burst. The rebinding tests pass unchanged | **Held** |
| An observer costs far less than a task | The shell's static RAM with a game and two observers fell from 57,184 bytes to 36,552. A further observer costs 32 bytes, up to each kind's compile-time count | **Held**, with that limit |
| A second game needs no game code | Neither the second lane nor the observer per lane changed a line of `src/` or `include/`. Both lanes play their own rules in their own tasks | **Held** |
| An observer of two games mixes their frames | One scoreboard subscribed to both lanes answered 9, not 16: lane 2's frame 1 overwrote lane 1's, since a `FrameBoard` is keyed by frame number | **Failed, as predicted**, the one prediction made to fail |
| The statistics selector hides state from the tests too | No test reads an actor's fields. The tests still include the state headers, to allocate an actor | **Held** |

**The mixed frames: predicted, and why the fix went into the message, not the topology.** The
prediction came from reading the protocol: a FRAME_CHANGED says which frame, but only its
envelope's `from` says which game. There were two ways out:
- **Change the topology: an observer of many games.** Every observer kind would key its frames by
  `from`, with a compile-time count of games per observer, and would change to do it. And its one
  figure would have to decide what "a figure over several games" means.
- **Keep the topology, and put what crosses lanes into a message.** This is what's built. An
  observer is an instance per game, which the kinds already allowed, so the fix changed no
  observer code. What a lane knows crosses as facts, not as a rounded answer: QUERY_STATS' reply
  carries each observer's total and its complete frames. The test combines two lanes exactly: 14
  over 2 frames and 9 over 1 give 23 over 3, which is 7. The average of the rounded averages, 7
  and 9, would be 8.

So a center-wide figure is **a new kind that composes by message**: it asks each lane's observers
for their stats and combines them. It is recorded, not built. It is Kay's answer, a new object
rather than a wider old one, and the stats' facts are what make it exact.

**Metrics** (the baseline is kay-oo at ad857ff; phase 2 is at 194fb52; phase 3 at 79cd042):

| | Baseline | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|
| Tests, host / with the POSIX port | 114 / – | 82 / 90 | 115 / 128 | 129 / 149 |
| Functions (lizard) | 143 | 111 | 149 | 165 |
| NLOC, `src/`, `include/`, `rtos/` | 1,283 | 1,079 | 1,602 | 1,792 |
| Highest cyclomatic complexity (limit 10) | 5 | 9 | 10 | 10, `GameActor_Receive` |
| Highest cognitive complexity (limit 7) | 4 | 5 | 5 | 5 |
| Function pointers | 6 | 1 | 1 | 1, the task entry |
| Release line coverage | 99.8% | 99.6% | 99.1% | 99.1% |
| Stack contract, game task, release / debug (budget 4608) | – | 3952 / 4224 | 3968 / 4320 | 3984 / 4320 |
| A request / a reply or event, bytes | – | 56 / 40 | 44 / 44 | 44 / 44 |
| Actor state: game / scoreboard / average, bytes | – | 104 (game) | 96 / 34 / 34 | 96 / 34 / 34 |
| The shell's static RAM, this host | – | – | 57,184: a game and two observers | 36,552 for the same; 55,760 for two lanes and four observers |
| Each further observer | – | – | about 18.8 KB | 32 bytes, up to the compile-time count |

Functions average 7.7 lines and a cyclomatic complexity of 1.9. Phase 4 took 29 commits, this
report's included: 11 `[make-change]`, 4 `[make-easy]` and 14 `[clean-up]`.

**RAM, as the lanes were added** (the shell's `s_shell`, release):

| Step | Bytes | The difference |
|---|---|---|
| A game and two observers, a task each (phase 3) | 57,184 | |
| The same, the observers sharing a task | 36,552 | −20,632 |
| Two lanes, with lane 1's observers only | 55,504 | +18,952: the second game's task |
| The routing table, 8 routes to 16 | 55,696 | +192: 8 routes of 24 bytes, bound or not |
| An observer instance per lane | 55,760 | +64: two more observers' state |

**What the change lost.**
- **Deadline order.** The observers outrank the games, so an observer's work delays a game, and
  the game drains the pinsetter. The delay is bounded only by the condition on observer kinds:
  short, bounded handling. An observer that does I/O breaks it, and needs a task of its own below
  the games.
- **"32 bytes an observer" has a ceiling.** Instances are arrays sized at compile time. Raising a
  kind's count costs each extra instance's state whether it's used or not, and every route costs
  24 bytes, bound or not.
- **The lifecycle has one more exception.** A game awaiting its rules answers "no game" to
  everything but NEW_GAME, the pinsetter's rolls and losses, and now QUERY_STATS: one more branch
  outside the lifecycle's switch, for a promise the protocol makes of every kind.
- **The vocabulary keeps the scorer's names.** `rules.h` still says `ScorerRules` and
  `SCORER_MAX_BALLS`. Renaming them would touch every file for no change in dependency, so it's
  left, and recorded.
- **16 bytes of release stack,** from the new values' accessors becoming calls into other files.
- **Each lane is its own world.** Nothing combines lanes yet but a test. The kind that would is
  recorded, not built.

**Surprises.**
1. **A kernel bug hung CI.** V11.1.0's POSIX port can end a cancelled thread holding its event's
   mutex. It's a race: rare on many cores, all but certain on one, and made likelier by phase 4's
   extra tasks. It was diagnosed with gdb, fixed by V11.2.0, and is now checked on one CPU in CI.
   Recovering meant rewriting the integration line, as CLAUDE.md prescribes.
2. **Stale objects, three times.** A file edited and restored from Windows within a second or two
   kept its old object across the WSL file bridge, twice. After the kernel upgrade, the fetched
   sources were older than the objects, so the one-CPU check still ran V11.1.0's code, and still
   hung. The local scripts now delete the project's objects before every build, and every build
   directory's kernel objects were deleted once. CI builds from nothing.
3. **Getting the backtrace was its own problem.** The hang didn't happen under gdb, and gdb run
   over many tests in one process produced a different failure: a scheduler restarted on the last
   test's task lists, which ctest never does, since it runs each test in a process of its own.
   With `ptrace_scope` at 1 and no sudo, the answer was a preloaded library that lets any process
   trace the test, so gdb could attach to a hung one from outside.
4. **An event answered with NOT_UNDERSTOOD turns a burst around.** The burst test's first run
   dropped 22 messages: the observers didn't list ROLL_HELD, and answered 26 NOT_UNDERSTOODs into
   the busy game's mailbox of 4. Every subscriber now lists the whole subscriber protocol.
5. **A fresh game didn't answer its statistics.** The protocol test the review asked for was the
   first to ask a game before its first NEW_GAME.
6. **The game and the shell both empty the outbox:** the game on entry, the shell after posting.
   The observers rely on the shell's. It's harmless, and uneven.
7. **Process:** `git stash pop` applied an old stash of kay-oo's work, because nothing had been
   stashed first. It was undone, and the stash kept intact. Staging is by path now, never
   `git add -u` or a stash.

**Mutation feedback at the phase stop.** Debug had 495 mutants: 478 killed and 17 survived, a
score of 96.6%, in 15 m 9 s. Release had the same 495 and the same 17 survivors, 96.6%, in 13 m
22 s. Phase 3 had 94.2% and 95.8%. The survivors:
- **Known classes, unchanged:** `Fault_Stop`'s two, the frame board's three, `NULL` against the
  message's own array for no new balls, the held list's shift reading one past a full list (now in
  `held_rolls.c`), the event's deliberately unpinned `seq`, and the scorer's three.
- **Equivalent, new with the statistics:** three loop bounds, `<` to `<=` in the complete-frame
  count and in the reopening, and `<=` or 42 frames in the catch-up. Each reads a frame past the
  last, which `Scorer_Frame` answers "not complete, 0", as it has since phase 2's fix.
- **Equivalent, new with the extraction:** `HeldRolls_Init`'s first refusal. Before a game no one
  can subscribe to hear it, and once a game starts, a refusal writes it before it's read.
- **A real gap, now pinned:** the `rolls_lost` in a game's statistics survived `+` to `-`, since
  the only test had losses of one kind. The new test has losses of both, and fails against that
  mutant made by hand.

**Host only; this would change on a target.**
- **The stack figures and the RAM.** Every task's 16 KB stack is the POSIX port's minimum, not the
  contract. On a target, an observer's saving is about 3.5 KB, not 18.8 KB, though the same
  fraction. The control blocks' sizes are this port's.
- **"Nothing dropped" in the burst tests.** Preemption on the POSIX port is simulated with
  signals, so the burst tests are good evidence on this host, not proof.
- **The kernel bug.** It was in the POSIX port's shutdown, which a target doesn't run.
- **Deadline order's cost.** An observer's worst-case handling time against the game's latency
  budget can only be measured on the target.
- **Not host-only:** the protocol, the routing, the lifecycle, the statistics, the call graph's
  shape, and every test of the pure core.

**Recorded, not built.**
- **A center-wide figure:** a new kind that composes the lanes' stats by message.
- **A stack per task role,** if a target's RAM forces it: a second switch, per role (game tasks,
  observer tasks), never per kind.
- **Live rebinding stays deferred.** Sends look the routing table up in the sender's task, so a
  rebind message to the shell wouldn't remove the race. It needs every send routed through one
  task, or an atomic update of the table.
- **A route's cost:** 24 bytes each, bound or not, so a table sized for a real center costs that
  per id.

### Kay's three properties, scored again

| Property | Enforced by the structure | Still convention | Overridden by Power of Ten |
|---|---|---|---|
| **Messaging** | As in phase 3, and now for observers that share a task: each keeps its own route, and gets its messages through the observers' mailbox, never a call from the game. Every kind answers QUERY_STATS, so its counters are messages too. **The observers have message semantics with call-like timing:** they outrank every game, so each event is handled before the game goes on, as a call would be. But it is still a message: copied, addressed by id, rebindable, and answerable with NOT_UNDERSTOOD | The tests call a kind's receive function directly, standing in for the host | The scorer is still a value inside the game, called, by design |
| **Local, protected state** | As in phase 3. The tests no longer read an actor's fields: they ask | The tests include the state headers, to allocate an actor, and could still read them | Static allocation: the host must know each actor's size |
| **Extreme late binding** | Who receives a message is the routing table's data, and what it means is the bound kind's. QUERY_FIGURE's contract says so, and QUERY_STATS gives the facts that mean the same for every kind. A second lane is a route and an instance, with no game code | Bindings are made at startup only | The kinds are a closed enum and one switch. Every instance is an array sized at compile time. A game task's stack is sized for the deepest kind |

**Decisions for phase 5: the QEMU target.**
- **Make the host-only figures real:** the stack contract without the host's allowances, the
  control blocks' sizes, and the RAM of two lanes.
- **Measure deadline order:** an observer's worst-case handling time against the game's latency
  budget, on real preemption. That decides whether the observers can stay above the games.
- **Rerun the burst tests** on the target, where preemption isn't simulated.
- **A stack per task role** becomes a decision once the target's RAM is known: built only if the
  game-sized observer stack doesn't fit.
- **CI:** the target stage from the gated-CI section's plan, as a gate beside the host's builds.

**Stop.** Phase 4 ends here, as the brief asks.

## A picky clean-up, before phase 5

One item per commit, in the brief's order: owners first, then tell-don't-ask, then composed
methods, then the dead-simple items, then comments. Each commit passed every gate, and each batch
was pushed and promoted as usual. The pass took 30 commits, this report's included: 29
`[clean-up]` and one `[make-change]`, a bug the pass found (below). One item, 5d, was added during
the pass: comments that state intent became names that reveal it.

**Metrics, before and after** (lizard and clang-tidy over `src/`, `include/` and `rtos/`):

| | Before (77db082) | After |
|---|---|---|
| NLOC | 1,792 | 1,875 |
| Functions | 165 | 183 |
| Average lines / cyclomatic complexity per function | 7.7 / 1.9 | 7.2 / 1.7 |
| Highest cyclomatic complexity (limit 10) | 10, `GameActor_Receive` | 10, `GameActor_Receive` |
| Largest function | `GameActor_Receive`, 32 lines | `GameActor_Receive`, 32 lines |
| Highest cognitive complexity (limit 7) | 5, the pinsetter's interrupt | 5, the pinsetter's interrupt |
| `scorer.c` / `game_actor.c` / `game_shell.c`, lines | 484 / 385 / 356 | 442 / 367 / 277 |
| Tests | 149 | 149: one deleted with its hazard, one added for the bug |
| Stack contract, game task, release / debug (budget 4608) | 3984 / 4320 | 4048 / 4320 |
| Stack contract, interrupt, release / debug (budget 3584) | 3232 / 3344 | 3232 / 3376 |
| The shell's static RAM, two lanes | 55,760 | 55,736 |
| Release line coverage | 99.1% | 99.1% |

The code grew by 83 NLOC while its functions shrank: 18 more of them, each shorter and simpler.
The largest function is unchanged. `GameActor_Receive` is one switch over the game's requests,
which the brief leaves alone. The game task's release stack grew 80 bytes, because each event is
now built once on the stack before the subscribers copy it. The interrupt's grew 32 bytes in
debug, from the call into the pinsetter.

**Every module touched, its job before and after:**

| Module | Before | After |
|---|---|---|
| `src/scorer.c` | Scores a game, and judges whether a variant's rules can be played | Scores a game, and tells its frames: rolled, edited, complete, and reopened |
| `src/rules.c` (new), `include/rules.h` | The vocabulary, with no code | Also judges whether rules can be played, and converts frame numbers to indexes and back |
| `src/message.c` (new) | – | Addresses events and replies, and decides who hears NOT_UNDERSTOOD |
| `src/outbox.c` | Stores what one message sends, and addresses each kind of answer itself | Stores what one message sends, with a shortcut for each way every kind answers |
| `src/game_actor.c` | The game's requests and lifecycle, plus loops over its subscribers, walks over the scorer's frames, and the held list's lost count | The game's requests and lifecycle: it decides what to tell, and its values and the scorer do the telling |
| `src/held_rolls.c` | A list the game asked field by field | Holds a roll or counts it lost, and describes a held roll |
| `src/subscribers.c` | A list the game looped over | Sends an event to each subscriber |
| `src/roll_edit.c` | An edit's arithmetic, and a guard for kay-oo's pointer API | An edit's arithmetic |
| `src/frame_board.c` | Rebuilds a game's frames from its events | Unchanged, through the frame-number helper |
| `rtos/game_shell.c` | The host: the routing table, posting, the drop count, the pinsetter's queues and ports, the tasks, the instance pools, and dispatch | Creates and wires the instances, the tasks, the router and the pinsetter, and dispatches |
| `rtos/router.c` (new) | – | Posts a message to whoever is bound at its id, and counts what it can't |
| `rtos/pinsetter.c`, `rtos/pinsetter_isr.c` (was `game_shell_isr.c`) | The interrupt's half, with the shell holding the rest | Feeds one game's task with the rolls and loss counts its interrupt takes |

**Tell, don't ask.**
- **Enforced by actor boundaries.** Between actors there is nothing to ask but a message. The game
  tells its subscribers every frame, held roll and loss; they never query it. The pinsetter tells
  the game what it counted, and never asks. Even the statistics are a question by message, which
  the asked kind answers as it chooses. The compiler can't see an actor's state from outside
  (`actor_state_is_hidden`), so tell-don't-ask between actors is structure, not discipline.
- **Still a discipline inside an actor.** The game calls its values and its scorer directly, and
  nothing stops it asking where it should tell. After this pass it asks only for decisions that
  are its own:
  - whether a held roll is waiting, to let them through;
  - whether the game is over, to refuse a NEW_GAME;
  - the score, for a reply;
  - the next ball's number, for a held roll's report;
  - whether the subscriber list is full, to refuse a subscription.
- **Letting held rolls through stays in the game** (item 2e). The decision needs the scorer: each
  held roll is played, and the first the scorer refuses stops the rest. Moving it into the held
  rolls would hand them the scorer, and a value that plays a game is the game.
- **Why kay-oo's phase 2 reason for declining it no longer applies.** Kay-oo declined because
  telling would have reshaped the core protocol with no failing test to pull it: a rejection
  outcome, the rule inside the frames, a reversed roll order, and frame creation moved out of
  `Game`. It named the feature that would pull it in: a validity rule only the frame can know,
  such as candlepin's three balls. That feature arrived in this experiment's phase 1, and it
  moved validation into the scorer then. What is left here needs no new protocol. The places to
  tell into already exist: `FrameEvents`, the outbox and messages. So each change is local,
  keeps behavior, and is done under green tests as a `[clean-up]`, and the principle and the
  discipline now point the same way. Each one deletes a question: a loop over the list, a walk
  over the scorer's frames, a full-list check, four field reads. Not lines, though: the four
  tell-don't-ask commits changed the code by −4, +1, +4 and +5 lines.

**The bug the pass found.** Moving the frame walks into the scorer (item 2c) showed that answering
QUERY_STATS before a game, added at the end of phase 4 (921165c), counted frames on a scorer never
started. It replayed whatever ball count and balls its memory held. The shell's instances are
static, so zeroed, and the protocol test's stack memory happened to pass. A test starting the game
in memory full of junk hit the scorer's assert. Before a game, the statistics now give the
counters only (45d56c1, `[make-change]`).

**Not worth doing as written, and why.**
- **2b folded into 1f.** Once the held rolls count their own overflow, the game has to call
  `HeldRolls_Hold` and read its answer, and the full-list check goes in the same change.
- **4d had two conversions left, not four.** The game's two went with its frame walks in 2c.
- **3f routes through `(id, Route)`,** not one `Route(...)` taking the kind, instance, mailbox and
  task. That would take five parameters, past ENG-3.1's four. `GameShell_HostedBy` builds a
  hosted route, and `Bind` builds an external one.
- **1b keeps NOT_UNDERSTOOD from the kind's own id.** `Envelope_ReplyTo` would address it from
  the request's `to`, which the host always makes the kind's id. But the observers' tests pin
  "from its own id", so the outbox overrides `from`, and behavior is unchanged.
- **2a changes the outbox's interleaving.** Events go out event by event now, not subscriber by
  subscriber. Each subscriber hears the same messages in the same order. No test pins the
  interleaving, and no host depends on it.
- **4e: the NULL-edit guards were deleted, not kept.** Power of Ten's rule 7 asks each function to
  check its parameters. Here it is applied where values arrive from outside, in messages. No
  function in the core NULL-checks a pointer its caller owns. The edit's pins are inline in the
  message, and its only caller passes a local.
- **`Scorer_Frame` and `Scorer_FramesStarted` stay,** now used only by the scorer's own tests,
  which ask about frames by example.
- **`Subscribers_Count` and `Subscribers_At` went,** since only the loops that 2a removed used
  them.

**Brought to you, not decided.**
- **A pinsetter per lane.** After 1d the pinsetter is bound by id: `Pinsetter_Start(pinsetter,
  game, task)`. A second one needs a pool of pinsetters in the shell and a way for an interrupt to
  say which lane it counted, such as `GameShell_PinsetterCountedFromIsr(lane, pins)`. That changes
  the public API, so it's a `[make-change]`, and it waits for your answer.
- **The held-roll policy in a pinsetter actor.** The pinsetter would send ROLL, hear the REPLY,
  and hold a refused roll itself. That is the natural Kay split: the game would stop holding what
  it refused, and "hold and retry" would belong to whoever has the rolls. The costs:
  - The pinsetter would need to learn when a correction is accepted, from a new event or a
    subscription to the game, before retrying.
  - Several rolls could be in flight, so replies would need matching by seq, and a roll sent while
    an earlier one waits could overtake it.
  - It would become an actor with a mailbox and a task, or a share of one, where it is an
    interrupt and two queues today.
  - The held rolls' order guarantee, which the game gets from its own list today, would have to
    hold across messages.

  Recorded, not built.
- **Naming, with a recommendation for each:**
  - **`SCORER_MAX_*` in `rules.h`:** the limits are the vocabulary's, not the scorer's. I'd rename
    them `GAME_MAX_*`, as one mechanical commit. It touches nearly every file.
  - **`FrameEvent` in `rules.h`:** it's the FRAME_CHANGED payload and the scorer's report, the
    shared vocabulary, so it stays.
  - **`RollEdit` in `bowling_types.h`:** it's the scorer's edit API, with a pointer, which only
    the scorer and the game use. The protocol's `EditPayload` doesn't use it. I'd move it to
    `scorer.h`.
  - **`GAME_OK` as the status an observer replies with:** `GameStatus` is the game's enum, and an
    observer borrows its OK. A protocol-level status, a small enum in `message.h` with the game's
    errors kept in `bowling_status.h`, would give every kind its own word. It changes the
    payload's type, so I'd bring it with phase 5 rather than here.

**Stop.** The clean-up pass ends here, for your review.

### The clean-up pass's decisions, applied (2026-09-29)

1. **A pinsetter per lane.** Each game's task holds its lane's pinsetter, with its own queues and
   its own lost count. A lane is a hosted game, counted from 0 in hosting order. The interrupt
   entry, `GameShell_PinsetterCountedFromIsr(lane, pins)`, finds the pinsetter with a bounds check
   and an index, and fault-stops for a lane no game is hosted at. Three tests:
   - lane 1's pinsetter feeds lane 1's game only, which failed before the change;
   - an unbound lane stops with the reason, which died on the kernel's assert before;
   - each lane counts its own losses, whose teeth were proved against a lookup that feeds every
     lane to lane 0.

   The interrupt's figures, before and after:

   | | Before | After |
   |---|---|---|
   | Its frames, `-O0` (the counting, the entry) | 48, 48 bytes | 48, 48 |
   | Its frames, `-O2` | 64, 16 | 64, 16 |
   | Stack contract, release / debug (budget 3584) | 3232 / 3376 | 3232 / 3376 |
   | Cognitive complexity (limit 7) | 5 | 5 |
   | Stack tripwire (96 bytes) | passes | passes |

   The shell's static RAM rose from 55,736 bytes to 56,120: the second lane's pinsetter.
2. **The held-roll policy stays in the game,** with the trade-offs recorded in the clean-up
   report.
3. **Names.** `SCORER_MAX_*` is `BOWLING_MAX_*`, except `SCORER_MAX_EVENTS`: it sizes the
   scorer's own report buffer, so it moved to `scorer.h` with its name. `RollEdit` moved to
   `scorer.h`. `FrameEvent` stays in `rules.h`. The protocol's own OK status comes with phase 5.

## Phase 5: a league night

**The QEMU target stage moves to phase 6.** Phase 5 changes what a target should measure: states
may become kinds, with different data and a different dispatch. The target's stack, RAM and
latency figures are worth taking once, of the design that survives phase 5.

**The driving feature: a league night on one lane.**
- **Practice.** After a new game starts, balls are counted but not scored, until a message ends
  practice. The practice count is in QUERY_STATS.
- **Pinsetter down.** A message says the machine is down. Pinsetter rolls are refused and
  counted; manual rolls and edits still work. A resume message brings the lane back.
- **Holding** becomes an explicit state, where today it's implied by a non-empty held list. No
  behavior changes.
- **Certified.** Once the scorer says the game is over and nothing is held, a certify message
  locks it. Edits are refused, and questions are still answered. Certifying is a decision, not a
  fact the scorer can work out, so it is stored. "Over" stays the scorer's to say.

Out of scope: a disputed ball awaiting a manager's approval, recorded as the next state if this
phase argues for more.

**The hypothesis, stated before it is tested.**
- **A protocol table per state replaces the lifecycle's conditionals.** Every (state, selector)
  pair gets an explicit answer, and the largest game function's complexity falls, even with four
  or five states.
- **The threshold.** The same selector means different things in three or more states. The
  pinsetter's roll is counted in practice, scored in play, queued while holding, refused while
  down, and refused once certified. That is where tables pay off over a switch.
- **`become:` pays only if states need different data.** A state as a kind at the id: practice
  needs no scorer, and a certified game could shrink to a read-only record.
- **Expected to fail:** pinsetter-down and holding stay separate states. If they want to merge,
  that is recorded.

**The baseline, at 27b7567:**

| | |
|---|---|
| NLOC, `src/`, `include/`, `rtos/` | 1,876 |
| Functions | 182 |
| `game_actor.c`: NLOC / functions | 323 / 33 |
| Highest cyclomatic complexity (limit 10) | 10, `GameActor_Receive`, 32 lines |
| The game's other dispatch functions | `GameActor_Handle` 5, `GameActor_BeforeAGame` 3 |
| `case` labels / `if`s in `game_actor.c` | 11 / 17 |
| Highest cognitive complexity (limit 7) | 5, the pinsetter's interrupt |
| The shell's dispatch cases | 5, one per kind |
| Stack contract, game task, release / debug | 4048 / 4320 |
| The shell's static RAM | 56,120 bytes |
| The game's state | 96 bytes |
| Tests | 152 |

### Interim report, after steps 1 and 2: the tables

**What changed.**
1. **A protocol table per lifecycle state** (`k_game_protocols[state][selector]`), with a row for
   every selector and one for a selector outside the protocol. BeforeAGame's chain and the
   special cases for NEW_GAME and QUERY_STATS are gone. `GameActor_Handle` is three lines: look up
   the meaning, answer, play.
2. **Holding is an explicit state.** It's set where holding starts, on the first pinsetter roll
   refused in play or a held roll refused again, and cleared when the last held roll is let
   through. The pinsetter's roll and the discard stopped asking whether anything is held: the
   state's row answers.

**The metrics, against the baseline (27b7567):**

| | Baseline | Step 1 | Step 2 |
|---|---|---|---|
| States | 2 (one implied) | 2 | 3 |
| Highest cyclomatic complexity in the game (limit 10) | 10, `GameActor_Receive` | 9, `GameActor_MakeThePlay` | 9 and 9, `GameActor_Answer` and `GameActor_MakeThePlay` |
| `if`s in `game_actor.c` | 17 | 12 | 10 |
| `case` labels in `game_actor.c` | 11 | 15 | 16 |
| `game_actor.c`, NLOC / functions | 323 / 33 | 368 / 35 | 385 / 35 |
| NLOC, `src/`, `include/`, `rtos/` | 1,876 | – | 1,940 |
| The tables, ROM | 60 bytes | 256 | 384 |
| Stack contract, game task, release / debug | 4048 / 4320 | – | 4048 / 4336 |
| The game's state / the shell's RAM | 96 / 56,120 | – | 96 / 56,120 |
| Tests | 152 | 152 | 152 |

**What the tables did, and didn't.**
- **The conditionals went.** Five of the game's 17 `if`s were the lifecycle, asked outright or
  through "is anything held". The largest function fell from 10 to 9, as predicted, with a third
  state added.
- **The switch didn't shrink; it split.** One switch over every request would have 13 cases, past
  ENG-3.1's 10. So an entry names an answer, which leaves the game as it is, and a play, which
  changes it, and each has a switch. The complexity limit, not the tables, decides the shape of
  the dispatch, as it did twice in phase 3. With the three states still to come, the plays will
  pass 9 cases again. The next split, if it comes, is by what a play touches (the rolls, or the
  lifecycle).
- **The cost is rows.** Three states of 16 rows is 48 lines, and 384 bytes of ROM. Each new state
  adds 16 of each, whether a selector means anything new there or not. That is the price of
  "every pair explicit", and it is paid in data, not in branches.
- **The threshold is already in view.** The pinsetter's roll means three things in three states:
  held before a game, played or held in play, and held behind the others while holding. The
  discard also means three: "no game", "nothing held" and a discard. Steps 3 to 5 take the
  pinsetter's roll to five.

**Found by writing every row out: two games can answer each other forever.** Before its first
game, a game answers everything but NEW_GAME, the pinsetter's messages and QUERY_STATS with a
"no game" reply. That includes events, replies and NOT_UNDERSTOOD. So a reply to an in-play game
is not understood, and the NOT_UNDERSTOOD it sends to a game awaiting rules gets a "no game"
reply. That reply is not understood again, and so on, for as long as both run. Phase 4 found the
same shape for observers and ROLL_HELD. Only a message between two games could start it, and no
sender does that today. Step 1 kept the behavior, since it was a `[make-easy]`. Brought to you
below.

**Brought to you before step 3, with a recommendation each.**
1. **The echo above.** I'd make it a `[make-change]` before step 3: before a game, events, replies
   and NOT_UNDERSTOOD get the answer they get in play (not understood, and a NOT_UNDERSTOOD is
   counted but never answered). "No game" stays the answer to requests.
2. **Whose state is "pinsetter down", the pinsetter's or the game's?**
   - **The pinsetter's.** With a pinsetter per lane, it knows it is down, and can refuse and count
     at the source. Manual rolls and edits keep working for free, since they never come through
     it. But the pinsetter isn't an actor: it has no id, no mailbox, and no way to hear "down" or
     answer QUERY_STATS. Making it one reopens the held-roll question you decided in the clean-up,
     and moves "refused and counted" out of the game's statistics.
   - **The game's.** Down is one more row in the game's table. The pinsetter's roll is refused
     and counted there, and the statistics already live there. It is the threshold's own example.
   - **I'd make it the game's for phase 5,** and record the pinsetter's as the alternative, with the
     held-roll question beside it. The phase's question is whether per-state tables pay off,
     and this keeps it about that.
3. **Resume after pinsetter down.** Back to in play always, with holding worked out again from the
   held list, is simpler, and it's the only one that stays right. Edits and discards still work
   while the machine is down, so an edit can let every held roll through. A remembered "holding"
   would then be stale. Working it out needs one question, at one transition: is anything held?
4. **Must a finished game be certified before the next NEW_GAME?** I'd say no. Certifying is a
   manager's decision, and a lane shouldn't stop because it wasn't made. An uncertified game
   simply ends uncertified. If a league requires it, that's a rule for a lane or league actor,
   not the game.
5. **Pinsetter down during practice.** I'd allow it. Practice pinsetter rolls are refused and
   counted, as in play, and resume goes back to practice. If down is the game's state, that means
   remembering practice, or a "down in practice" row. If it's the pinsetter's, it comes for free:
   one more point for the pinsetter.
6. **The lifecycle in the game, or in a lane actor that owns the game?** Practice and
   certification look like the lane's business: neither is a rule of bowling, and practice needs
   no scorer. I'd keep them in the game through step 5. Then the `become:` spike measures the
   other shape, a state as its own kind, which is close to what a lane actor would be.
7. **The protocol's own OK status** (from the clean-up decisions). I'd do it as a `[make-easy]`
   before step 3, since practice's and certification's replies are the first that aren't the
   game's own.
8. **Practice counts manual rolls too.** "Balls are counted but not scored": I've read that as both
   the pinsetter's and the manual ones. Say if you meant only the pinsetter's.

**Stop.** Steps 1 and 2 end here, for your review.
