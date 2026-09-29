#include "game_shell.h"

#include <stdatomic.h>

#include "task.h"

#include "game_shell_ports.h"
#include "posix_stack.h"
#include "running_average.h"
#include "scoreboard.h"

/* How many messages a hosted actor's mailbox holds. */
#define GAME_SHELL_MAILBOX 4U
#define GAME_SHELL_PINSETTER_ROLLS 32U

/* The actors the shell can host, each in a task of its own, and the instances of each kind. */
#define GAME_SHELL_HOSTED 3U
#define GAME_SHELL_GAMES 1U
#define GAME_SHELL_SCOREBOARDS 1U
#define GAME_SHELL_RUNNING_AVERAGES 1U

/* The POSIX port sizes the task's pthread stack from this, and a pthread stack is at least
 * PTHREAD_STACK_MIN, so the port's minimum. */
#define GAME_SHELL_TASK_STACK_WORDS configMINIMAL_STACK_SIZE

_Static_assert(GAME_SHELL_PINSETTER_ROLLS >= SCORER_MAX_BALLS,
               "the pinsetter's queue holds a whole game of rolls");
_Static_assert(GAME_SHELL_TASK_STACK_WORDS * sizeof(StackType_t) >= GAME_SHELL_TASK_STACK_BUDGET,
               "a hosted actor's stack holds its budget");

/* A row of the routing table: the kind at an id, which of that kind's instances, the mailbox its
 * messages go to, and the task to wake, when the shell hosts it. */
typedef struct {
    ActorKind kind;
    uint8_t instance;
    QueueHandle_t mailbox;
    TaskHandle_t task;
} GameShellRoute;

/* One hosted actor's task: its mailbox, the message it is handling and what that sends. */
typedef struct {
    ActorId id;
    QueueHandle_t mailbox;
    StaticQueue_t mailbox_queue;
    uint8_t mailbox_storage[GAME_SHELL_MAILBOX * sizeof(Message)];
    Message message;
    Outbox outbox;
    PosixStack stack_paint;
    TaskHandle_t task;
    StaticTask_t task_buffer;
    StackType_t stack[GAME_SHELL_TASK_STACK_WORDS];
} GameShellHosted;

typedef struct {
    GameShellRoute routes[GAME_SHELL_ACTORS];
    GameShellHosted hosted[GAME_SHELL_HOSTED];
    uint8_t hosted_count;
    GameActor games[GAME_SHELL_GAMES];
    Scoreboard scoreboards[GAME_SHELL_SCOREBOARDS];
    uint8_t scoreboard_count;
    RunningAverage running_averages[GAME_SHELL_RUNNING_AVERAGES];
    uint8_t running_average_count;
    atomic_uint_least16_t outputs_dropped; /* written by the hosted tasks, read by any */
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

/* Into the mailbox of whoever is bound at the message's "to", waking its task. */
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

/* For the game, the pinsetter's rolls, then its count of those it lost, before a message waiting
 * with them; for everyone, its own mailbox. */
static bool GameShell_TakeMessage(GameShell *self, GameShellHosted *hosted)
{
    const bool is_the_game = (hosted->id == GAME_SHELL_GAME_ID);
    return (is_the_game && (GameShell_TakePinsetterRoll(self, &hosted->message) ||
                            GameShell_TakeLostReport(self, &hosted->message))) ||
           (xQueueReceive(hosted->mailbox, &hosted->message, 0U) == pdPASS);
}

static void GameShell_Task(void *parameter)
{
    GameShellHosted *hosted = (GameShellHosted *)parameter;
    PosixStack_Paint(&hosted->stack_paint);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (GameShell_TakeMessage(&s_shell, hosted)) {
            GameShell_Dispatch(&s_shell, &hosted->message, &hosted->outbox);
        }
    }
}

/* An actor of `kind`, its instance already started, at `id`, in a task of its own. */
static void GameShell_HostInstance(ActorId id, ActorKind kind, uint8_t instance,
                                   UBaseType_t priority)
{
    GameShell *self = &s_shell;
    configASSERT(self->hosted_count < GAME_SHELL_HOSTED);
    GameShellHosted *hosted = &self->hosted[self->hosted_count];
    self->hosted_count++;
    hosted->id = id;
    hosted->mailbox = xQueueCreateStatic(GAME_SHELL_MAILBOX, sizeof(Message),
                                         hosted->mailbox_storage, &hosted->mailbox_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    hosted->task = xTaskCreateStatic(&GameShell_Task, "actor", GAME_SHELL_TASK_STACK_WORDS, hosted,
                                     priority, hosted->stack, &hosted->task_buffer);
    self->routes[id].kind = kind;
    self->routes[id].instance = instance;
    self->routes[id].mailbox = hosted->mailbox;
    self->routes[id].task = hosted->task;
}

void GameShell_Start(UBaseType_t priority)
{
    GameShell *self = &s_shell;
    for (uint8_t id = 0U; id < GAME_SHELL_ACTORS; id++) {
        self->routes[id] = s_no_route;
    }
    self->hosted_count = 0U;
    self->scoreboard_count = 0U;
    self->running_average_count = 0U;
    atomic_init(&self->outputs_dropped, 0U);
    self->pinsetter = xQueueCreateStatic(GAME_SHELL_PINSETTER_ROLLS, sizeof(Pins),
                                         self->pinsetter_storage, &self->pinsetter_queue);
    self->lost_report = xQueueCreateStatic(1U, sizeof(uint16_t), self->lost_report_storage,
                                           &self->lost_report_queue);
    GameActor_Init(&self->games[0], GAME_SHELL_GAME_ID);
    GameShell_HostInstance(GAME_SHELL_GAME_ID, ACTOR_KIND_GAME, 0U, priority);
    self->ports.pinsetter = self->pinsetter;
    self->ports.lost_report = self->lost_report;
    self->ports.game_task = self->routes[GAME_SHELL_GAME_ID].task;
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

void GameShell_HostScoreboard(ActorId id, UBaseType_t priority)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    const uint8_t instance = GameShell_StartScoreboard(&s_shell, id);
    GameShell_HostInstance(id, ACTOR_KIND_SCOREBOARD, instance, priority);
}

void GameShell_HostRunningAverage(ActorId id, UBaseType_t priority)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    const uint8_t instance = GameShell_StartRunningAverage(&s_shell, id);
    GameShell_HostInstance(id, ACTOR_KIND_RUNNING_AVERAGE, instance, priority);
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
    return PosixStack_DeepestUse(&s_shell.hosted[0].stack_paint);
}

BaseType_t GameShell_Send(const Message *message, TickType_t wait)
{
    return GameShell_Post(&s_shell, message, wait) ? pdPASS : pdFAIL;
}
