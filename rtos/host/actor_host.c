#include "actor_host.h"

#include "task.h"

#include "fault.h"

#include "actor_host_lanes.h"
#include "pinsetter.h"
#include "port_memory.h"
#include "router.h"
#include "task_stack.h"
#include "game_actor_state.h"
#include "running_average_state.h"
#include "scoreboard_state.h"

/* How many messages a hosting task's mailbox holds, for all the actors it hosts. */
#define ACTOR_HOST_MAILBOX 4U

/* The instances of each kind the shell can host. Each game has a task of its own; the observers
 * share one. */
#define ACTOR_HOST_GAMES 2U
#define ACTOR_HOST_SCOREBOARDS 2U
#define ACTOR_HOST_RUNNING_AVERAGES 2U

/* The most one message makes an observer send: the observers' task's outbox holds that. */
#define ACTOR_HOST_OBSERVER_MOST_SENT 1U

_Static_assert(ACTOR_HOST_TASK_STACK_WORDS * sizeof(StackType_t) >= ACTOR_HOST_TASK_STACK_BUDGET,
               "a hosting task's stack holds its budget");
_Static_assert(SCOREBOARD_MOST_SENT <= ACTOR_HOST_OBSERVER_MOST_SENT,
               "the observers' outbox holds a scoreboard's largest burst");
_Static_assert(RUNNING_AVERAGE_MOST_SENT <= ACTOR_HOST_OBSERVER_MOST_SENT,
               "the observers' outbox holds a running average's largest burst");

/* A hosting task: one mailbox for every actor it hosts, the message it is handling, and the outbox
 * that message's sends go to, in storage its role sizes; for a game's task, its lane's pinsetter.
 * Actors are not tasks. */
typedef struct {
    Pinsetter *pinsetter; /* none for the observers' task */
    UBaseType_t priority;
    QueueHandle_t mailbox;
    StaticQueue_t mailbox_queue;
    uint8_t mailbox_storage[ACTOR_HOST_MAILBOX * sizeof(Message)];
    Message message;
    Outbox outbox;
    TaskStack stack_paint;
    TaskHandle_t task;
    StaticTask_t task_buffer;
    StackType_t stack[ACTOR_HOST_TASK_STACK_WORDS];
} ActorHostTask;

/* A game's task: a game sends up to GAME_OUTBOX_CAPACITY for one message. */
typedef struct {
    ActorHostTask task;
    Message outbox_storage[GAME_OUTBOX_CAPACITY];
    Pinsetter pinsetter;
} ActorHostGameTask;

/* The observers' task: every observer, sharing one stack, one mailbox and a small outbox. */
typedef struct {
    ActorHostTask task;
    Message outbox_storage[ACTOR_HOST_OBSERVER_MOST_SENT];
} ActorHostObserverTask;

typedef struct {
    Router router;
    ActorHostGameTask game_tasks[ACTOR_HOST_GAMES];
    ActorHostObserverTask observer_task;
    GameActor games[ACTOR_HOST_GAMES];
    uint8_t game_count;
    Scoreboard scoreboards[ACTOR_HOST_SCOREBOARDS];
    uint8_t scoreboard_count;
    RunningAverage running_averages[ACTOR_HOST_RUNNING_AVERAGES];
    uint8_t running_average_count;
} ActorHost;

/* Every instance, task, stack and queue the host has, in the one region the port places it in. */
static ActorHost s_shell PORT_SHELL_MEMORY;

/* The one late-binding point: the kind bound at the message's "to" decides what it means, and
 * which instance's receive function hears it. */
static void ActorHost_Dispatch(ActorHost *self, const Message *message, Outbox *outbox)
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

static bool ActorHost_TakeMessage(ActorHostTask *host)
{
    if ((host->pinsetter != NULL) && Pinsetter_Take(host->pinsetter, &host->message)) {
        return true;
    }
    return xQueueReceive(host->mailbox, &host->message, 0U) == pdPASS;
}

static void ActorHost_Task(void *parameter)
{
    ActorHostTask *host = (ActorHostTask *)parameter;
    TaskStack_Paint(&host->stack_paint, host->stack);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (ActorHost_TakeMessage(host)) {
            host->outbox.count = 0U;
            ActorHost_Dispatch(&s_shell, &host->message, &host->outbox);
            Router_Deliver(&s_shell.router, &host->outbox);
        }
    }
}

/* A hosting task, its outbox in `outbox_storage`, of `most_sent` messages, at `priority`. */
static void ActorHostTask_Start(ActorHostTask *host, Message *outbox_storage, uint8_t most_sent,
                                UBaseType_t priority)
{
    Outbox_Init(&host->outbox, outbox_storage, most_sent);
    host->priority = priority;
    host->mailbox = xQueueCreateStatic(ACTOR_HOST_MAILBOX, sizeof(Message), host->mailbox_storage,
                                       &host->mailbox_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    host->task = xTaskCreateStatic(&ActorHost_Task, "actors", ACTOR_HOST_TASK_STACK_WORDS, host,
                                   priority, host->stack, &host->task_buffer);
}

static void ActorHost_RequireBindableId(ActorId id)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < ROUTER_IDS));
}

/* An actor of `kind`, its instance already started, hosted by `host`. */
static Route ActorHost_HostedBy(ActorKind kind, uint8_t instance, const ActorHostTask *host)
{
    const Route route = { kind, instance, host->mailbox, host->task };
    return route;
}

/* A game at `id`, and the task of its own that hosts it: which instance it is. */
static uint8_t ActorHost_StartAGame(ActorHost *self, ActorId id, UBaseType_t priority)
{
    configASSERT(self->game_count < ACTOR_HOST_GAMES);
    const uint8_t instance = self->game_count;
    self->game_count++;
    ActorHostGameTask *game_task = &self->game_tasks[instance];
    ActorHostTask_Start(&game_task->task, game_task->outbox_storage, GAME_OUTBOX_CAPACITY,
                        priority);
    Pinsetter_Start(&game_task->pinsetter, id, game_task->task.task);
    game_task->task.pinsetter = &game_task->pinsetter;
    GameActor_Init(&self->games[instance], id);
    return instance;
}

static void ActorHost_HostAGame(ActorHost *self, ActorId id, UBaseType_t priority)
{
    ActorHost_RequireBindableId(id);
    const uint8_t instance = ActorHost_StartAGame(self, id, priority);
    const ActorHostTask *host = &self->game_tasks[instance].task;
    Router_Bind(&self->router, id, ActorHost_HostedBy(ACTOR_KIND_GAME, instance, host));
}

static void ActorHost_StartTheObserversTask(ActorHost *self, UBaseType_t priority)
{
    ActorHostObserverTask *observers = &self->observer_task;
    ActorHostTask_Start(&observers->task, observers->outbox_storage,
                        ACTOR_HOST_OBSERVER_MOST_SENT, priority);
    observers->task.pinsetter = NULL;
}

/* Why the observers must outrank every game is in ARCHITECTURE.md, section 3. */
static void ActorHost_RequireObserversOutrankEveryGame(const ActorHost *self)
{
    for (uint8_t i = 0U; i < self->game_count; i++) {
        if (self->observer_task.task.priority <= self->game_tasks[i].task.priority) {
            Fault_Stop("ActorHost: the observers' task must outrank every game's, or a game's "
                       "burst overflows their mailbox");
        }
    }
}

static void ActorHost_UnbindEveryIdAndForgetEveryInstance(ActorHost *self)
{
    Router_Reset(&self->router);
    self->game_count = 0U;
    self->scoreboard_count = 0U;
    self->running_average_count = 0U;
}

void ActorHost_Start(UBaseType_t game_priority, UBaseType_t observer_priority)
{
    ActorHost *self = &s_shell;
    ActorHost_UnbindEveryIdAndForgetEveryInstance(self);
    ActorHost_HostAGame(self, ACTOR_HOST_GAME_ID, game_priority);
    ActorHost_StartTheObserversTask(self, observer_priority);
    ActorHost_RequireObserversOutrankEveryGame(self);
}

static uint8_t ActorHost_StartScoreboard(ActorHost *self)
{
    configASSERT(self->scoreboard_count < ACTOR_HOST_SCOREBOARDS);
    Scoreboard_Init(&self->scoreboards[self->scoreboard_count]);
    return self->scoreboard_count++;
}

static uint8_t ActorHost_StartRunningAverage(ActorHost *self)
{
    configASSERT(self->running_average_count < ACTOR_HOST_RUNNING_AVERAGES);
    RunningAverage_Init(&self->running_averages[self->running_average_count]);
    return self->running_average_count++;
}

void ActorHost_HostGame(ActorId id, UBaseType_t priority)
{
    ActorHost_HostAGame(&s_shell, id, priority);
    ActorHost_RequireObserversOutrankEveryGame(&s_shell);
}

void ActorHost_HostScoreboard(ActorId id)
{
    ActorHost_RequireBindableId(id);
    const uint8_t instance = ActorHost_StartScoreboard(&s_shell);
    const ActorHostTask *host = &s_shell.observer_task.task;
    const Route route = ActorHost_HostedBy(ACTOR_KIND_SCOREBOARD, instance, host);
    Router_Bind(&s_shell.router, id, route);
}

void ActorHost_HostRunningAverage(ActorId id)
{
    ActorHost_RequireBindableId(id);
    const uint8_t instance = ActorHost_StartRunningAverage(&s_shell);
    const ActorHostTask *host = &s_shell.observer_task.task;
    const Route route = ActorHost_HostedBy(ACTOR_KIND_RUNNING_AVERAGE, instance, host);
    Router_Bind(&s_shell.router, id, route);
}

void ActorHost_Bind(ActorId id, QueueHandle_t queue)
{
    ActorHost_RequireBindableId(id);
    const Route external = { ACTOR_KIND_EXTERNAL, 0U, queue, NULL };
    Router_Bind(&s_shell.router, id, external);
}

Pinsetter *ActorHost_PinsetterOfLane(ActorHostLane lane)
{
    if (lane >= s_shell.game_count) {
        Fault_Stop("ActorHost: a pinsetter counted a roll at a lane no game is hosted at");
    }
    return &s_shell.game_tasks[lane].pinsetter;
}

uint16_t ActorHost_OutputsDropped(void)
{
    return Router_Dropped(&s_shell.router);
}

size_t ActorHost_TaskStackUsed(void)
{
    const uint8_t game = Router_RouteTo(&s_shell.router, ACTOR_HOST_GAME_ID)->instance;
    return TaskStack_DeepestUse(&s_shell.game_tasks[game].task.stack_paint);
}

BaseType_t ActorHost_Send(const Message *message, TickType_t wait)
{
    return Router_Post(&s_shell.router, message, wait) ? pdPASS : pdFAIL;
}
