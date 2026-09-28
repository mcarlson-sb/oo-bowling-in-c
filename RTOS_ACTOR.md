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
