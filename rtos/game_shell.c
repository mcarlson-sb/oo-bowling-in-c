#include "game_shell.h"

#include <stdatomic.h>

#include "task.h"

#include "game_shell_ports.h"
#include "posix_stack.h"

#define GAME_SHELL_COMMANDS 4U
#define GAME_SHELL_PINSETTER_ROLLS 32U

/* The POSIX port sizes the task's pthread stack from this, and a pthread stack is at least
 * PTHREAD_STACK_MIN, so the port's minimum. */
#define GAME_SHELL_TASK_STACK_WORDS configMINIMAL_STACK_SIZE

_Static_assert(GAME_SHELL_PINSETTER_ROLLS >= SCORER_MAX_BALLS,
               "the pinsetter's queue holds a whole game of rolls");
_Static_assert(GAME_SHELL_TASK_STACK_WORDS * sizeof(StackType_t) >= GAME_SHELL_TASK_STACK_BUDGET,
               "the game task's stack holds its budget");

/* A row of the routing table: the kind at an id, the queue its messages go to, and the task to
 * wake, when the shell runs it. */
typedef struct {
    ActorKind kind;
    QueueHandle_t mailbox;
    TaskHandle_t task;
} GameShellRoute;

typedef struct {
    GameActor actor;
    GameOutbox outbox;
    Message message;
    GameShellRoute routes[GAME_SHELL_ACTORS];
    atomic_uint_least16_t outputs_dropped; /* written by the game task, read by any */
    QueueHandle_t commands;
    StaticQueue_t commands_queue;
    uint8_t commands_storage[GAME_SHELL_COMMANDS * sizeof(Message)];
    QueueHandle_t pinsetter;
    StaticQueue_t pinsetter_queue;
    uint8_t pinsetter_storage[GAME_SHELL_PINSETTER_ROLLS * sizeof(Pins)];
    QueueHandle_t lost_report;
    StaticQueue_t lost_report_queue;
    uint8_t lost_report_storage[sizeof(uint16_t)];
    GameShellPorts ports;
    TaskHandle_t task;
    StaticTask_t task_buffer;
    StackType_t stack[GAME_SHELL_TASK_STACK_WORDS];
} GameShell;

static GameShell s_shell;

static const GameShellRoute s_no_route = { ACTOR_KIND_NONE, NULL, NULL };

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

static void GameShell_Deliver(GameShell *self)
{
    for (uint8_t i = 0U; i < self->outbox.count; i++) {
        if (!GameShell_Post(self, &self->outbox.items[i], 0U)) {
            GameShell_CountDropped(self);
        }
    }
}

/* The one late-binding point: the kind at the message's "to" decides who receives it. */
static void GameShell_Dispatch(GameShell *self, const Message *message)
{
    switch (GameShell_RouteTo(self, message->envelope.to)->kind) {
    case ACTOR_KIND_GAME:
        GameActor_Handle(&self->actor, message, &self->outbox);
        GameShell_Deliver(self);
        break;
    case ACTOR_KIND_EXTERNAL:
    case ACTOR_KIND_NONE:
        GameShell_CountDropped(self);
        break;
    }
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

/* The pinsetter's rolls, then its count of those it lost, before a command waiting with them. */
static bool GameShell_TakeMessage(GameShell *self, Message *message)
{
    return GameShell_TakePinsetterRoll(self, message) ||
           GameShell_TakeLostReport(self, message) ||
           (xQueueReceive(self->commands, message, 0U) == pdPASS);
}

static void GameShell_HandleEverythingWaiting(GameShell *self)
{
    while (GameShell_TakeMessage(self, &self->message)) {
        GameShell_Dispatch(self, &self->message);
    }
}

static void GameShell_Task(void *parameter)
{
    GameShell *self = (GameShell *)parameter;
    PosixStack_Paint();
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        GameShell_HandleEverythingWaiting(self);
    }
}

void GameShell_Start(ScorerVariant variant, CountRule rule, UBaseType_t priority)
{
    GameShell *self = &s_shell;
    GameActor_Init(&self->actor, GAME_SHELL_GAME_ID, variant, rule);
    for (uint8_t id = 0U; id < GAME_SHELL_ACTORS; id++) {
        self->routes[id] = s_no_route;
    }
    atomic_init(&self->outputs_dropped, 0U);
    self->commands = xQueueCreateStatic(GAME_SHELL_COMMANDS, sizeof(Message),
                                        self->commands_storage, &self->commands_queue);
    self->pinsetter = xQueueCreateStatic(GAME_SHELL_PINSETTER_ROLLS, sizeof(Pins),
                                         self->pinsetter_storage, &self->pinsetter_queue);
    self->lost_report = xQueueCreateStatic(1U, sizeof(uint16_t), self->lost_report_storage,
                                           &self->lost_report_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    self->task = xTaskCreateStatic(&GameShell_Task, "game", GAME_SHELL_TASK_STACK_WORDS, self,
                                   priority, self->stack, &self->task_buffer);
    self->routes[GAME_SHELL_GAME_ID].kind = ACTOR_KIND_GAME;
    self->routes[GAME_SHELL_GAME_ID].mailbox = self->commands;
    self->routes[GAME_SHELL_GAME_ID].task = self->task;
    self->ports.pinsetter = self->pinsetter;
    self->ports.lost_report = self->lost_report;
    self->ports.game_task = self->task;
    GameShell_ResetIsr();
}

void GameShell_Bind(ActorId id, QueueHandle_t queue)
{
    configASSERT((id != ACTOR_ID_NONE) && (id < GAME_SHELL_ACTORS));
    s_shell.routes[id].kind = ACTOR_KIND_EXTERNAL;
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
    return PosixStack_DeepestUse();
}

BaseType_t GameShell_Send(const Message *message, TickType_t wait)
{
    return GameShell_Post(&s_shell, message, wait) ? pdPASS : pdFAIL;
}
