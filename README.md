# OO in C: the Bowling Kata, as actors

[![Gate](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/workflows/gate.yml/badge.svg?branch=integration%2Frtos-actor)](https://github.com/mcarlson-sb/oo-bowling-in-c/actions/workflows/gate.yml?query=branch%3Aintegration%2Frtos-actor)

The badge is the gate's status on `integration/rtos-actor`, where every push is gated.
`rtos-actor` has no runs of its own, because it only ever holds commits that already passed
this gate.

A bowling scorer in C11, written as Alan Kay's objects: actors that only exchange messages, keep
their state to themselves, and bind late. It is also written under JPL's Power of Ten:
- no function pointers, except the one FreeRTOS requires for a task's entry;
- no heap;
- every call target known to the static call graph.

The two pull against each other, and where they meet is the point of the code. Calls stay early
and fully analyzable. What binds late is data:
- who receives a message (a routing table, keyed by actor id);
- what a message means (the kind bound at that id);
- what the rules of the game are (they arrive in a message).

The problem is small on purpose, so the design is the thing to read. For the pictures, see
[ARCHITECTURE.md](ARCHITECTURE.md). [STATE_PATTERN.md](STATE_PATTERN.md) covers the state
machines, and why they are enums and tables rather than the State pattern. The experiment that
got here, phase by phase, with its measurements, is in [RTOS_ACTOR.md](RTOS_ACTOR.md), and the
experiment before it is in [KAY.md](KAY.md). The State-pattern design this replaced is intact
on the `kay-oo` and `main` branches.

## Build and test

Needs GCC, CMake 3.20 or later, and Ninja. CMake downloads GoogleTest, and FreeRTOS-Kernel
V11.2.0, on the first configure.

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux (or WSL), that also builds the FreeRTOS shell on the POSIX port, and its integration
tests, `rtos_tests`. Elsewhere, only the pure core and its tests build (`OO_C_RTOS` switches
it). The same tests run in three more configurations:

| Build | Configure with | What it adds |
|---|---|---|
| UBSan | `-DOO_C_SANITIZE=ON` | Undefined behavior traps, including an array read out of bounds |
| Release | `-DCMAKE_BUILD_TYPE=Release` | `NDEBUG`: the asserts compile out, and the guards behind them still hold |
| TSan | `-DOO_C_TSAN=ON` | Races between the RTOS tasks. Linux only; on GitHub's runners it needs `sudo sysctl vm.mmap_rnd_bits=28` first |

Warnings are errors, in the library and the tests alike.

### The gate

GitHub Actions runs every gate on each push to `integration/rtos-actor`
(`.github/workflows/gate.yml`), and fast-forwards `rtos-actor` to that commit only if all of
them pass:
- **Builds:** debug, release, UBSan and TSan.
- **Every commit:** since the last promotion, each commit is built and tested.
- **Line coverage, at least 95%:** of the library and the shell, in a release build
  (`tools/coverage_gate.py`).
- **ENG-3.1's limits, with lizard:** a cyclomatic complexity of at most 10, at most 50 lines
  and at most 4 parameters, over `src`, `include` and `rtos`.
- **Cognitive complexity, with clang-tidy 18:** at most 7.
- **No function pointers,** checked over clang's AST (`tools/check_function_pointers.py`). The
  only exemption is the task entry, marked on its line.
- **The stack tripwires:** `-Wstack-usage` fails the build on a frame over 320 bytes, or over
  96 on the interrupt side.
- **The stack contract** (`tools/stack_depth.py`): the deepest path from each task entry,
  through GCC's static call graph and the kernel's, plus measured allowances for the host's C
  library and signal frames, against the budgets in `rtos/game_shell.h`. It fails on anything it
  can't bound: recursion, an unbounded frame, or an indirect call.

Mutation testing (`tools/mutation.sh`, with Mull) is feedback, run by hand at each phase's
stop. It is not a gate.

## What the code does

| Part | Where | What it is |
|---|---|---|
| The scorer core | `include/scorer.h`, `src/scorer.c`, `src/roll_edit.c` | A pure value: the rules it was started with, and the balls. It plays any rules the scorer can hold: frames, balls a frame, pins a rack, bonus balls by clearing ball, and a count rule, the pins still standing, off a full rack, that count as a clear. Rolls, edits (replacing, inserting or deleting balls) and questions about the frames |
| The vocabulary | `include/bowling_types.h`, `include/rules.h` | What every part speaks: pins, scores, a variant's rules, the news of one frame, and the limits. The protocol and the observers depend on these, not on the scorer |
| The protocol | `include/message.h`, `include/outbox.h`, `src/outbox.c` | One message type for every actor: an envelope (selector, from, to, seq) and a payload of that selector's fields. How every kind replies, and answers NOT_UNDERSTOOD |
| The game actor | `include/game_actor.h`, `src/game_actor.c` | One game, and everything that may change it, as messages. NEW_GAME carries the rules. It holds pinsetter rolls the game refuses, until a correction lets them through, and tells its subscribers every frame that changes |
| The scoreboard and the running average | `include/scoreboard.h`, `include/running_average.h`, `src/` | Two kinds of subscriber. Each rebuilds the frames from the events it hears, and answers QUERY_SCORE its own way: the total, or the average |
| The shell | `rtos/game_shell.h`, `rtos/game_shell.c` | The actor host, on FreeRTOS. The routing table binds each id to a kind, an instance and a mailbox, and each hosted actor gets a task of its own. One switch, on the kind at a message's `to`, calls that kind's receive function |
| The interrupt side | `rtos/game_shell_isr.c` | The pinsetter's interrupt, which counts each roll into a queue, and a full queue's losses into a one-slot report |

Everything is statically allocated. Before a game, anything but a NEW_GAME or a QUERY_STATS is
answered "no game". Rules the scorer can't play are refused. A selector a kind doesn't respond
to is answered NOT_UNDERSTOOD and counted.

## Follow one roll

1. The pinsetter's interrupt calls `GameShell_PinsetterCountedFromIsr(7)`. The roll goes into
   the pinsetter's queue, and the game task gets a notification.
2. The game task wakes. It takes the roll as a `MSG_PINSETTER_ROLL` from no one, to the game's
   id, and dispatches it: the kind bound at that id is `ACTOR_KIND_GAME`, so
   `GameActor_Handle` receives it.
3. The game is in play, so the table maps the selector to its pinsetter-roll request. The roll
   goes to the scorer, which accepts it and reports the frames it completed.
4. For each subscriber id, the game writes a `MSG_FRAME_CHANGED` to its outbox. It knows the
   subscribers only by id, not what they are.
5. The shell posts each output to the mailbox bound at its `to`. A scoreboard's task wakes,
   dispatches the event to `Scoreboard_Handle`, and remembers the frame. A recording double's
   queue just keeps it.

Had the scorer refused the roll, the game would have held it, and told its subscribers
`MSG_ROLL_HELD`. A correction would later let it through.

## Why this is object-oriented

Kay's three properties, and where each one stands:
- **Messaging.** Every interaction is a message, and nothing calls into an actor's state from
  outside. Things with a lifetime of their own, or that run concurrently, are actors: the game
  and its observers. The scorer is a value inside the game, and frames are values too.
- **Local, protected state.** Each actor's struct is defined in `src/`, where only the library,
  the shell and the tests can see it. The public headers have an incomplete type, and the test
  `actor_state_is_hidden` fails if a definition moves back.
- **Late binding.** Senders address ids. Which kind sits at an id is the routing table's to
  say, and the kind decides what a message means: the same QUERY_SCORE gets a game's total, a
  scoreboard's total or an average. The rules are data in a message, so a variant never
  compiled in needs no code.

What Power of Ten overrides, and what it costs, is in RTOS_ACTOR.md's phase 3 report:
- **The one switch on the kind** is the single late-binding point: every call target stays
  visible, but every hosted task's stack is sized for the deepest kind.
- **Each kind's switch on the selector** is the smell the constitution calls a Duplicate
  Switch Case, whose usual remedy, a function-pointer table, is ruled out.
- **One message type for every kind** makes a 20-byte event a 44-byte one.

## Public and private headers

- **`include/`** is the public interface: the scorer, the protocol, and each actor's functions,
  with an incomplete type for its state.
- **`src/`** has the private headers: each actor's state (`*_state.h`), the frame board, and
  the scorer's edit arithmetic.
- **`rtos/`** is the shell: its public header, and the private one its interrupt side uses.

## Files

| Path | Holds |
|---|---|
| `include/` | The public headers |
| `src/` | The pure core, the actors and their private headers: no RTOS, no function pointers, no heap |
| `rtos/` | The FreeRTOS configuration, the actor host and its interrupt side, and the POSIX port's stand-in for a stack high-water mark |
| `test/` | GoogleTest suites for the scorer, each actor, and the shell on the POSIX port. Independent references, for ten-pin, candlepin and any rules. The rules presets the tests send. The hidden-state probe |
| `tools/` | The gates' scripts, the mutation-testing script and its Mull configuration, and the ruleset that guards `rtos-actor` |
| `RTOS_ACTOR.md` | The experiment's record: baselines, decisions, and phase reports with their metrics |

## License

[MIT](LICENSE).
