#include "game_shell.h"

#include <stdatomic.h>

#include "task.h"

#include "game_shell_ports.h"
#include "posix_stack.h"
#include "game_actor_state.h"
#include "running_average_state.h"
#include "scoreboard_state.h"

/* How many messages a hosting task's mailbox holds, for all the actors it hosts. */
#define GAME_SHELL_MAILBOX 4U
#define GAME_SHELL_PINSETTER_ROLLS 32U

/* The instances of each kind the shell can host. Each game has a task of its own; the observers
 * share one. */
#define GAME_SHELL_GAMES 1U
#define GAME_SHELL_SCOREBOARDS 1U
#define GAME_SHELL_RUNNING_AVERAGES 1U

/* The most one message makes an observer send: the observers' task's outbox holds that. */
#define GAME_SHELL_OBSERVER_MOST_SENT 1U

/* The POSIX port sizes a task's pthread stack from this, and a pthread stack is at least
 * PTHREAD_STACK_MIN, so the port's minimum. */
#define GAME_SHELL_TASK_STACK_WORDS configMINIMAL_STACK_SIZE

_Static_assert(GAME_SHELL_PINSETTER_ROLLS >= SCORER_MAX_BALLS,
               "the pinsetter's queue holds a whole game of rolls");
_Static_assert(GAME_SHELL_TASK_STACK_WORDS * sizeof(StackType_t) >= GAME_SHELL_TASK_STACK_BUDGET,
               "a hosting task's stack holds its budget");
_Static_assert(SCOREBOARD_MOST_SENT <= GAME_SHELL_OBSERVER_MOST_SENT,
               "the observers' outbox holds a scoreboard's largest burst");
_Static_assert(RUNNING_AVERAGE_MOST_SENT <= GAME_SHELL_OBSERVER_MOST_SENT,
               "the observers' outbox holds a running average's largest burst");

/* A row of the routing table: the kind at an id, which of that kind's instances, and the mailbox
 * and task of the task that hosts it; for an external actor, only the queue it reads. */
typedef struct {
    ActorKind kind;
    uint8_t instance;
    QueueHandle_t mailbox;
    TaskHandle_t task;
} GameShellRoute;

/* A hosting task: one mailbox for every actor it hosts, the message it is handling, and the outbox
 * that message's sends go to, in storage its role sizes. Actors are not tasks. */
typedef struct {
    bool takes_the_pinsetter;
    QueueHandle_t mailbox;
    StaticQueue_t mailbox_queue;
    uint8_t mailbox_storage[GAME_SHELL_MAILBOX * sizeof(Message)];
    Message message;
    Outbox outbox;
    PosixStack stack_paint;
    TaskHandle_t task;
    StaticTask_t task_buffer;
    StackType_t stack[GAME_SHELL_TASK_STACK_WORDS];
} GameShellTask;

/* A game's task: a game sends up to GAME_OUTBOX_CAPACITY for one message. */
typedef struct {
    GameShellTask task;
    Message outbox_storage[GAME_OUTBOX_CAPACITY];
} GameShellGameTask;

/* The observers' task: every observer, sharing one stack, one mailbox and a small outbox. */
typedef struct {
    GameShellTask task;
    Message outbox_storage[GAME_SHELL_OBSERVER_MOST_SENT];
} GameShellObserverTask;

typedef struct {
    GameShellRoute routes[GAME_SHELL_ACTORS];
    GameShellGameTask game_tasks[GAME_SHELL_GAMES];
    GameShellObserverTask observer_task;
    GameActor games[GAME_SHELL_GAMES];
    Scoreboard scoreboards[GAME_SHELL_SCOREBOARDS];
    uint8_t scoreboard_count;
    RunningAverage running_averages[GAME_SHELL_RUNNING_AVERAGES];
    uint8_t running_average_count;
    atomic_uint_least16_t outputs_dropped; /* written by the hosting tasks, read by any */
    QueueHandle_t pinsetter;
    StaticQueue_t pinsetter_queue;
    uint8_t pinsetter_storage[GAME_SHELL_PINSETTER_ROLLS * sizeof(Pins)];
    QueueHandle_t lost_report;
    StaticQueue_t lost_report_queue;
    uint8_t lost_report_storage[sizeof(uint16_t)];
    GameShellPorts ports;
} GameShell;

static GameShell s_shell;

static const GameShellRoute s_no_route = { ACTOR_KIND_NONE, 0U, NULL, NULL };

static const GameShellRoute *GameShell_RouteTo(const GameShell *self, ActorId id)
{
    return (id < GAME_SHELL_ACTORS) ? &self->routes[id] : &s_no_route;
}

static void GameShell_CountDropped(GameShell *self)
{
    (void)atomic_fetch_add_explicit(&self->outputs_dropped, 1U, memory_order_relaxed);
}

/* Into the mailbox of whoever is bound at the message's "to", waking the task that hosts it. */
static bool GameShell_Post(const GameShell *self, const Message *message, TickType_t wait)
{
    const GameShellRoute *route = GameShell_RouteTo(self, message->envelope.to);
    if ((route->mailbox == NULL) || (xQueueSend(route->mailbox, message, wait) != pdPASS)) {
        return false;
    }
    if (route->task != NULL) {
        xTaskNotifyGive(route->task);
    }
    return true;
}

static void GameShell_Deliver(GameShell *self, const Outbox *outbox)
{
    for (uint8_t i = 0U; i < outbox->count; i++) {
        if (!GameShell_Post(self, &outbox->items[i], 0U)) {
            GameShell_CountDropped(self);
        }
    }
}

/* The one late-binding point: the kind bound at the message's "to" decides what it means, and
 * which instance's receive function hears it. */
static void GameShell_Dispatch(GameShell *self, const Message *message, Outbox *outbox)
{
    const GameShellRoute *route = GameShell_RouteTo(self, message->envelope.to);
    outbox->count = 0U;
    switch (route->kind) {
    case ACTOR_KIND_GAME:
        GameActor_Handle(&self->games[route->instance], message, outbox);
        break;
    case ACTOR_KIND_SCOREBOARD:
        Scoreboard_Handle(&self->scoreboards[route->instance], message, outbox);
        break;
    case ACTOR_KIND_RUNNING_AVERAGE:
        RunningAverage_Handle(&self->running_averages[route->instance], message, outbox);
        break;
    case ACTOR_KIND_EXTERNAL:
    case ACTOR_KIND_NONE:
        GameShell_CountDropped(self);
        break;
    }
    GameShell_Deliver(self, outbox);
}

/* From the pinsetter, which has no id: it hears no reply. */
static void Envelope_FromThePinsetter(Envelope *envelope, Selector selector)
{
    envelope->selector = selector;
    envelope->from = ACTOR_ID_NONE;
    envelope->to = GAME_SHELL_GAME_ID;
    envelope->seq = 0U;
}

static bool GameShell_TakePinsetterRoll(GameShell *self, Message *message)
{
    Pins pins;
    if (xQueueReceive(self->pinsetter, &pins, 0U) != pdPASS) {
        return false;
    }
    Envelope_FromThePinsetter(&message->envelope, MSG_PINSETTER_ROLL);
    message->payload.roll.pins = pins;
    return true;
}

static bool GameShell_TakeLostReport(GameShell *self, Message *message)
{
    uint16_t lost;
    if (xQueueReceive(self->lost_report, &lost, 0U) != pdPASS) {
        return false;
    }
    Envelope_FromThePinsetter(&message->envelope, MSG_ROLLS_LOST);
    message->payload.rolls_lost.lost = lost;
    return true;
}

/* For the task that takes the pinsetter, its rolls, then its count of those it lost, before a
 * message waiting with them; for every task, its own mailbox. */
static bool GameShell_TakeMessage(GameShell *self, GameShellTask *host)
{
    return (host->takes_the_pinsetter && (GameShell_TakePinsetterRoll(self, &host->message) ||
                                          GameShell_TakeLostReport(self, &host->message))) ||
           (xQueueReceive(host->mailbox, &host->message, 0U) == pdPASS);
}

static void GameShell_Task(void *parameter)
{
    GameShellTask *host = (GameShellTask *)parameter;
    PosixStack_Paint(&host->stack_paint);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (GameShell_TakeMessage(&s_shell, host)) {
            GameShell_Dispatch(&s_shell, &host->message, &host->outbox);
        }
    }
}

/* A hosting task, its outbox in `outbox_storage`, of `most_sent` messages, at `priority`. */
static void GameShellTask_Start(GameShellTask *host, Message *outbox_storage, uint8_t most_sent,
                                UBaseType_t priority)
{
    Outbox_Init(&host->outbox, outbox_storage, most_sent);
    host->mailbox = xQueueCreateStatic(GAME_SHELL_MAILBOX, sizeof(Message), host->mailbox_storage,
                                       &host->mailbox_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    host->task = xTaskCreateStatic(&GameShell_Task, "actors", GAME_SHELL_TASK_STACK_WORDS, host,
                                   priority, host->stack, &host->task_buffer);
}

/* An actor of `kind`, its instance already started, at `id`, hosted by `host`. */
static void GameShell_Route(ActorId id, ActorKind kind, uint8_t instance,
                            const GameShellTask *host)
{
    GameShellRoute *route = &s_shell.routes[id];
    route->kind = kind;
    route->instance = instance;
    route->mailbox = host->mailbox;
    route->task = host->task;
}

static void GameShell_StartTheGame(GameShell *self, UBaseType_t priority)
{
    GameShellGameTask *game_task = &self->game_tasks[0];
    game_task->task.takes_the_pinsetter = true;
    GameShellTask_Start(&game_task->task, game_task->outbox_storage, GAME_OUTBOX_CAPACITY,
                        priority);
    GameActor_Init(&self->games[0], GAME_SHELL_GAME_ID);
    GameShell_Route(GAME_SHELL_GAME_ID, ACTOR_KIND_GAME, 0U, &game_task->task);
}

static void GameShell_StartTheObserversTask(GameShell *self, UBaseType_t priority)
{
    GameShellObserverTask *observers = &self->observer_task;
    observers->task.takes_the_pinsetter = false;
    GameShellTask_Start(&observers->task, observers->outbox_storage,
                        GAME_SHELL_OBSERVER_MOST_SENT, priority);
}

void GameShell_Start(UBaseType_t game_priority, UBaseType_t observer_priority)
{
    GameShell *self = &s_shell;
    for (uint8_t id = 0U; id < GAME_SHELL_ACTORS; id++) {
        self->routes[id] = s_no_route;
    }
    self->scoreboard_count = 0U;
    self->running_average_count = 0U;
    atomic_init(&self->outputs_dropped, 0U);
    self->pinsetter = xQueueCreateStatic(GAME_SHELL_PINSETTER_ROLLS, sizeof(Pins),
                                         self->pinsetter_storage, &self->pinsetter_queue);
    self->lost_report = xQueueCreateStatic(1U, sizeof(uint16_t), self->lost_report_storage,
                                           &self->lost_report_queue);
    GameShell_StartTheGame(self, game_priority);
    GameShell_StartTheObserversTask(self, observer_priority);
    self->ports.pinsetter = self->pinsetter;
    self->ports.lost_report = self->lost_report;
    self->ports.game_task = self->game_tasks[0].task.task;
    GameShell_ResetIsr();
}

static uint8_t GameShell_StartScoreboard(GameShell *self, ActorId id)
{
    configASSERT(self->scoreboard_count < GAME_SHELL_SCOREBOARDS);
    Scoreboard_Init(&self->scoreboards[self->scoreboard_count], id);
    return self->scoreboard_count++;
}

static uint8_t GameShell_StartRunningAverage(GameShell *self, ActorId id)
{
    configASSERT(self->running_average_count < GAME_SHELL_RUNNING_AVERAGES);
    RunningAverage_Init(&self->running_averages[self->running_average_count], id);
    return self->running_average_count++;
}

void GameShell_HostScoreboard(ActorId id)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    const uint8_t instance = GameShell_StartScoreboard(&s_shell, id);
    GameShell_Route(id, ACTOR_KIND_SCOREBOARD, instance, &s_shell.observer_task.task);
}

void GameShell_HostRunningAverage(ActorId id)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    const uint8_t instance = GameShell_StartRunningAverage(&s_shell, id);
    GameShell_Route(id, ACTOR_KIND_RUNNING_AVERAGE, instance, &s_shell.observer_task.task);
}

void GameShell_Bind(ActorId id, QueueHandle_t queue)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    s_shell.routes[id].kind = ACTOR_KIND_EXTERNAL;
    s_shell.routes[id].instance = 0U;
    s_shell.routes[id].mailbox = queue;
    s_shell.routes[id].task = NULL;
}

const GameShellPorts *GameShell_Ports(void)
{
    return &s_shell.ports;
}

uint16_t GameShell_OutputsDropped(void)
{
    return (uint16_t)atomic_load_explicit(&s_shell.outputs_dropped, memory_order_relaxed);
}

size_t GameShell_TaskStackUsed(void)
{
    return PosixStack_DeepestUse(&s_shell.game_tasks[0].task.stack_paint);
}

BaseType_t GameShell_Send(const Message *message, TickType_t wait)
{
    return GameShell_Post(&s_shell, message, wait) ? pdPASS : pdFAIL;
}
