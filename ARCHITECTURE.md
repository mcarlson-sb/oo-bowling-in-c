# Architecture

How the code fits together, as it is on `rtos-actor`: the layers, the protocol, how a message
finds its receiver, the state each actor keeps, and a roll's journey through all of it. The
reasons, with the measurements, are in [RTOS_ACTOR.md](RTOS_ACTOR.md).

## 1. The layers

```
 rtos/  the shell: FreeRTOS on the POSIX port
 +------------------------------------------------------------------------------------+
 |  pinsetter               routing table          hosting tasks                     |
 |  (pinsetter.c, _isr.c),  id -> kind, instance,  +--------------+ +--------------+ |
 |  bound to a game's id:   mailbox, task          | a game's     | | observers'   | |
 |  rolls, lost report -->                         | own task     | | shared task  | |
 |                          GameShell_Post         | mailbox      | | mailbox      | |
 |                          GameShell_Dispatch     | outbox (41)  | | outbox (1)   | |
 |                                                 +------+-------+ +------+-------+ |
 +-----------------------------------------------------|-----------------|-----------+
                                    one switch on the kind at "to"
 src/  the pure core: no RTOS, no function pointers, no heap
 +-----------------------------------------------------|-----------------|-----------+
 |  GameActor_Handle          Scoreboard_Handle          RunningAverage_Handle        |
 |      |                         |                         |                        |
 |      |  Scorer (a value)       |  FrameBoard (a value)   |  FrameBoard            |
 |      |  roll_edit              |                         |                        |
 |      |  HeldRolls, Subscribers |                         |                        |
 |      |  (values)               |                         |                        |
 |  outbox: replies, NOT_UNDERSTOOD, shared by every kind                            |
 +------------------------------------------------------------------------------------+
 each component's include/: the vocabulary (value_objects), the protocol (message.h, outbox.h),
           the scorer and each actor's functions; the actors depend on the vocabulary, not the scorer
```

The core knows nothing of FreeRTOS. It takes a message and an outbox, and writes what it sends
to the outbox. The shell moves messages between queues and the core.

## 2. The protocol

Every message, request, reply or event, is one type:

```c
typedef struct {
    Envelope envelope;       /* selector, from, to, seq: 8 bytes */
    union {                  /* the selector's fields only */
        NewGamePayload new_game;   /* the rules: 7 bytes */
        RollPayload roll;
        EditPayload edit;          /* up to 30 new balls, inline: 33 bytes */
        RollsLostPayload rolls_lost;
        ReplyPayload reply;
        FrameEvent frame;
        RollHeldPayload roll_held;
        NotUnderstoodPayload not_understood;
    } payload;
} Message;                   /* 44 bytes */
```

- **Nothing in a message points anywhere,** because a queue copies it. The edit's balls are
  inline for that reason, and they are what makes every message 44 bytes.
- **Several kinds answer the same selectors.** QUERY_FIGURE gets a game's total, a scoreboard's
  total or a running average's average.
- **A reply is from the id its request was sent to,** to the request's `from`, with its `seq`.
- **An event carries no seq.**

## 3. Routing and dispatch

The router (`rtos/router/`) holds the routing table, and the shell wires it. Each row is the
kind bound at an id, which instance of that kind, the mailbox of the task that hosts it, and that
task, to wake.

- **Posting** looks up the row at the message's `to`, puts the message in that mailbox with no
  wait, and wakes the task. It's a table lookup with no switch. An id with no row, one past the
  table, or a full mailbox makes the message undeliverable, and it is counted.
- **Dispatch** is the one switch on the kind at `to`. It calls that kind's receive function on
  that instance: the single late-binding point, and every target is a direct call the call graph
  sees.
- **Each kind** then looks the selector up in its own protocol table (`respondsTo:`). A
  selector it doesn't list is not understood, and counted. It's answered NOT_UNDERSTOOD only if
  it is a request from someone: an answer (a reply, an event, a NOT_UNDERSTOOD or statistics) is
  never answered, by any kind in any state, so two kinds can't trade answers forever.
  `message.c` classifies each selector once.

Bindings are made at startup, before the scheduler runs:
- **`GameShell_Start`** hosts the game at id 1.
- **`GameShell_HostScoreboard` and `GameShell_HostRunningAverage`** host observers.
- **`GameShell_Bind`** binds an external actor: a queue the actor reads itself, as a test's
  client or a recording double does.

**The hosting tasks, and why the observers outrank every game.** Each game has a task of its
own, and every observer shares one task, with one mailbox of 4. For one message, a game can send
up to 41, with no wait. With the observers' task above every game, it preempts the game after
each post, so its mailbox never holds more than one of the game's events. Level with a game, or
below it, the mailbox would hold everything the game sends until the game blocks, which only a
busy period bounds. So `GameShell_Start` stops if the observers don't outrank every game. The
price is deadline order: an observer's work delays a game, which is why an observer kind's
handling must be short and bounded.

**Messages are copied, so nothing in one points anywhere.** A queue copies a message into the
receiver's mailbox, so a pointer into the sender's memory could outlive what it points to. An
edit's new balls are carried inline for that reason.

## 4. The state each actor keeps

Each actor's struct is defined in a `*_state.h` beside it, in `src/actors/<kind>/`. Only the library, the shell and the tests
can see it; everywhere else it is an incomplete type.

| Actor | Its state | Bytes |
|---|---|---|
| Game | Its id; its lifecycle's decision (awaiting rules, practice, in play or certified); a `Scorer` value (the rules, the longest game, the balls); its subscribers' ids (a `Subscribers` value); the rolls held mid-game and why the first was refused (a `HeldRolls` value); whether the pinsetter is down; lost, refused, practice and not-understood counts. Holding and over are derived, not stored | 104 |
| Scoreboard | A `FrameBoard` (each frame's score and whether it is complete), a not-understood count. No id: the routing table binds it to one, and its answers are addressed from the message they answer | 32 |
| Running average | The same as a scoreboard; only its answer differs | 32 |

The scorer keeps only the balls, and works everything else out by walking them through the
rules each time it is asked. So it is a plain value: copying it copies the game, and an edit
replays into a copy, so a refused edit changes nothing.

## 5. A roll's journey

```
 pinsetter interrupt
   GameShell_PinsetterCountedFromIsr(0, 7)
     xQueueSendFromISR(pinsetter queue)   full? count it lost, overwrite the lost report
     vTaskNotifyGiveFromISR(game task)
 game task (GameShell_Task)
   takes MSG_PINSETTER_ROLL, from no one, to the game's id
   GameShell_Dispatch: the kind at the game's id is ACTOR_KIND_GAME
     GameActor_Handle
       in play: the table says "pinsetter roll"
       Scorer_Roll: accepted; frame 1 complete, 7
       the outbox: MSG_FRAME_CHANGED to each subscriber id
   GameShell_Post, per output: the row at "to" -> its mailbox, wake its task
 scoreboard task
   takes MSG_FRAME_CHANGED
   GameShell_Dispatch: ACTOR_KIND_SCOREBOARD
     Scoreboard_Handle: the table says "frame changed"; FrameBoard_Hear
```

## 6. The stack

A task's stack is sized from the static call graph. The deepest path from `GameShell_Task` goes
through the dispatch and the game's edit replay: 896 bytes in release, 1392 in debug. On this
host, allowances for the C library and the port's signal frames are added on top. The budget is
4608 bytes, checked by `tools/stack_depth.py` in CI.

All hosted tasks run that one entry, so every task is sized for the deepest kind, the game's,
while a scoreboard's own path is 48 bytes in release. That is the stack cost of keeping one
switch on the kind. A painted stack cross-checks the budget, measuring 1583 bytes at most.

## 7. The tests

| Suite | What it checks |
|---|---|
| `scorer_test.cpp` | The scorer by example. Random games against three independent references (ten-pin, candlepin, and any rules), 5000 to 10000 games each. Edits against fresh games of the edited balls. Every refusal of rules it can't play |
| `game_actor_test.cpp` | The game by messages alone: subscribers, the held rolls, the lifecycle, NOT_UNDERSTOOD, and the outbox's worst case, which fills it exactly |
| `scoreboard_actor_test.cpp`, `running_average_test.cpp` | The two observer kinds, by messages alone |
| `game_shell_test.cpp` | On the POSIX port: the queues, the interrupt, lost and dropped messages, ordering, the painted stack, and the rebinding proof (the same game and sender, with a scoreboard, an average or a recording double at the id) |
| `actor_state_is_hidden` | A compile that must fail: allocating an actor with only the components' public `include/` directories on the path |
| `protocol_is_free_of_the_scorer` | A compile that must fail: naming the scorer's type with only the protocol's and the observers' headers included |
| `protocol_test.cpp` | What the protocol promises of every kind: each answers QUERY_STATS, in any state |
