#include "game_shell.h"

#include "task.h"

#include "fault.h"

#include "pinsetter.h"
#include "posix_stack.h"
#include "router.h"
#include "game_actor_state.h"
#include "running_average_state.h"
#include "scoreboard_state.h"

/* How many messages a hosting task's mailbox holds, for all the actors it hosts. */
#define GAME_SHELL_MAILBOX 4U

/* The instances of each kind the shell can host. Each game has a task of its own; the observers
 * share one. */
#define GAME_SHELL_GAMES 2U
#define GAME_SHELL_SCOREBOARDS 2U
#define GAME_SHELL_RUNNING_AVERAGES 2U

/* The most one message makes an observer send: the observers' task's outbox holds that. */
#define GAME_SHELL_OBSERVER_MOST_SENT 1U

/* The POSIX port sizes a task's pthread stack from this, and a pthread stack is at least
 * PTHREAD_STACK_MIN, so the port's minimum. */
#define GAME_SHELL_TASK_STACK_WORDS configMINIMAL_STACK_SIZE

_Static_assert(GAME_SHELL_TASK_STACK_WORDS * sizeof(StackType_t) >= GAME_SHELL_TASK_STACK_BUDGET,
               "a hosting task's stack holds its budget");
_Static_assert(SCOREBOARD_MOST_SENT <= GAME_SHELL_OBSERVER_MOST_SENT,
               "the observers' outbox holds a scoreboard's largest burst");
_Static_assert(RUNNING_AVERAGE_MOST_SENT <= GAME_SHELL_OBSERVER_MOST_SENT,
               "the observers' outbox holds a running average's largest burst");

/* A hosting task: one mailbox for every actor it hosts, the message it is handling, and the outbox
 * that message's sends go to, in storage its role sizes; for a game's task, its lane's pinsetter.
 * Actors are not tasks. */
typedef struct {
    Pinsetter *pinsetter; /* none for the observers' task */
    UBaseType_t priority;
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
    Pinsetter pinsetter;
} GameShellGameTask;

/* The observers' task: every observer, sharing one stack, one mailbox and a small outbox. */
typedef struct {
    GameShellTask task;
    Message outbox_storage[GAME_SHELL_OBSERVER_MOST_SENT];
} GameShellObserverTask;

typedef struct {
    Router router;
    GameShellGameTask game_tasks[GAME_SHELL_GAMES];
    GameShellObserverTask observer_task;
    GameActor games[GAME_SHELL_GAMES];
    uint8_t game_count;
    Scoreboard scoreboards[GAME_SHELL_SCOREBOARDS];
    uint8_t scoreboard_count;
    RunningAverage running_averages[GAME_SHELL_RUNNING_AVERAGES];
    uint8_t running_average_count;
} GameShell;

static GameShell s_shell;

/* The one late-binding point: the kind bound at the message's "to" decides what it means, and
 * which instance's receive function hears it. */
static void GameShell_Dispatch(GameShell *self, const Message *message, Outbox *outbox)
{
    const Route *route = Router_RouteTo(&self->router, message->envelope.to);
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
        Router_CountDropped(&self->router);
        break;
    }
}

static bool GameShell_TakeMessage(GameShellTask *host)
{
    if ((host->pinsetter != NULL) && Pinsetter_Take(host->pinsetter, &host->message)) {
        return true;
    }
    return xQueueReceive(host->mailbox, &host->message, 0U) == pdPASS;
}

static void GameShell_Task(void *parameter)
{
    GameShellTask *host = (GameShellTask *)parameter;
    PosixStack_Paint(&host->stack_paint);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (GameShell_TakeMessage(host)) {
            host->outbox.count = 0U;
            GameShell_Dispatch(&s_shell, &host->message, &host->outbox);
            Router_Deliver(&s_shell.router, &host->outbox);
        }
    }
}

/* A hosting task, its outbox in `outbox_storage`, of `most_sent` messages, at `priority`. */
static void GameShellTask_Start(GameShellTask *host, Message *outbox_storage, uint8_t most_sent,
                                UBaseType_t priority)
{
    Outbox_Init(&host->outbox, outbox_storage, most_sent);
    host->priority = priority;
    host->mailbox = xQueueCreateStatic(GAME_SHELL_MAILBOX, sizeof(Message), host->mailbox_storage,
                                       &host->mailbox_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    host->task = xTaskCreateStatic(&GameShell_Task, "actors", GAME_SHELL_TASK_STACK_WORDS, host,
                                   priority, host->stack, &host->task_buffer);
}

static void GameShell_RequireBindableId(ActorId id)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
}

/* An actor of `kind`, its instance already started, hosted by `host`. */
static Route GameShell_HostedBy(ActorKind kind, uint8_t instance, const GameShellTask *host)
{
    const Route route = { kind, instance, host->mailbox, host->task };
    return route;
}

static void GameShell_Route(GameShell *self, ActorId id, Route route)
{
    Router_Bind(&self->router, id, route);
}

/* A game at `id`, and the task of its own that hosts it: which instance it is. */
static uint8_t GameShell_StartAGame(GameShell *self, ActorId id, UBaseType_t priority)
{
    configASSERT(self->game_count < GAME_SHELL_GAMES);
    const uint8_t instance = self->game_count;
    self->game_count++;
    GameShellGameTask *game_task = &self->game_tasks[instance];
    GameShellTask_Start(&game_task->task, game_task->outbox_storage, GAME_OUTBOX_CAPACITY,
                        priority);
    Pinsetter_Start(&game_task->pinsetter, id, game_task->task.task);
    game_task->task.pinsetter = &game_task->pinsetter;
    GameActor_Init(&self->games[instance], id);
    return instance;
}

static void GameShell_HostAGame(GameShell *self, ActorId id, UBaseType_t priority)
{
    GameShell_RequireBindableId(id);
    const uint8_t instance = GameShell_StartAGame(self, id, priority);
    const GameShellTask *host = &self->game_tasks[instance].task;
    GameShell_Route(self, id, GameShell_HostedBy(ACTOR_KIND_GAME, instance, host));
}

static void GameShell_StartTheObserversTask(GameShell *self, UBaseType_t priority)
{
    GameShellObserverTask *observers = &self->observer_task;
    GameShellTask_Start(&observers->task, observers->outbox_storage,
                        GAME_SHELL_OBSERVER_MOST_SENT, priority);
    observers->task.pinsetter = NULL;
}

/* Why the observers must outrank every game is in ARCHITECTURE.md, section 3. */
static void GameShell_RequireObserversOutrankEveryGame(const GameShell *self)
{
    for (uint8_t i = 0U; i < self->game_count; i++) {
        if (self->observer_task.task.priority <= self->game_tasks[i].task.priority) {
            Fault_Stop("GameShell: the observers' task must outrank every game's, or a game's "
                       "burst overflows their mailbox");
        }
    }
}

static void GameShell_UnbindEveryIdAndForgetEveryInstance(GameShell *self)
{
    Router_Reset(&self->router);
    self->game_count = 0U;
    self->scoreboard_count = 0U;
    self->running_average_count = 0U;
}

void GameShell_Start(UBaseType_t game_priority, UBaseType_t observer_priority)
{
    GameShell *self = &s_shell;
    GameShell_UnbindEveryIdAndForgetEveryInstance(self);
    GameShell_HostAGame(self, GAME_SHELL_GAME_ID, game_priority);
    GameShell_StartTheObserversTask(self, observer_priority);
    GameShell_RequireObserversOutrankEveryGame(self);
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

void GameShell_HostGame(ActorId id, UBaseType_t priority)
{
    GameShell_HostAGame(&s_shell, id, priority);
    GameShell_RequireObserversOutrankEveryGame(&s_shell);
}

void GameShell_HostScoreboard(ActorId id)
{
    GameShell_RequireBindableId(id);
    const uint8_t instance = GameShell_StartScoreboard(&s_shell, id);
    GameShell_Route(&s_shell, id, GameShell_HostedBy(ACTOR_KIND_SCOREBOARD, instance,
                                                     &s_shell.observer_task.task));
}

void GameShell_HostRunningAverage(ActorId id)
{
    GameShell_RequireBindableId(id);
    const uint8_t instance = GameShell_StartRunningAverage(&s_shell, id);
    GameShell_Route(&s_shell, id, GameShell_HostedBy(ACTOR_KIND_RUNNING_AVERAGE, instance,
                                                     &s_shell.observer_task.task));
}

void GameShell_Bind(ActorId id, QueueHandle_t queue)
{
    GameShell_RequireBindableId(id);
    const Route external = { ACTOR_KIND_EXTERNAL, 0U, queue, NULL };
    GameShell_Route(&s_shell, id, external);
}

Pinsetter *GameShell_PinsetterOfLane(GameShellLane lane)
{
    return &s_shell.game_tasks[lane].pinsetter;
}

uint16_t GameShell_OutputsDropped(void)
{
    return Router_Dropped(&s_shell.router);
}

size_t GameShell_TaskStackUsed(void)
{
    const uint8_t game = Router_RouteTo(&s_shell.router, GAME_SHELL_GAME_ID)->instance;
    return PosixStack_DeepestUse(&s_shell.game_tasks[game].task.stack_paint);
}

BaseType_t GameShell_Send(const Message *message, TickType_t wait)
{
    return Router_Post(&s_shell.router, message, wait) ? pdPASS : pdFAIL;
}
