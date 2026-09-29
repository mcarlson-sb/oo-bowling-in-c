#include "game_shell.h"

#include "task.h"

#define GAME_SHELL_COMMANDS 4U
#define GAME_SHELL_PINSETTER_ROLLS 32U

_Static_assert(GAME_SHELL_PINSETTER_ROLLS >= SCORER_MAX_BALLS,
               "the pinsetter's queue holds a whole game of rolls");

typedef struct {
    GameActor actor;
    GameOutbox outbox;
    GameMessage message;
    QueueHandle_t commands;
    StaticQueue_t commands_queue;
    uint8_t commands_storage[GAME_SHELL_COMMANDS * sizeof(GameMessage)];
    QueueHandle_t pinsetter;
    StaticQueue_t pinsetter_queue;
    uint8_t pinsetter_storage[GAME_SHELL_PINSETTER_ROLLS * sizeof(Pins)];
    TaskHandle_t task;
    StaticTask_t task_buffer;
    StackType_t stack[configMINIMAL_STACK_SIZE];
} GameShell;

static GameShell s_shell;

static void GameShell_Deliver(const GameOutbox *outbox)
{
    for (uint8_t i = 0U; i < outbox->count; i++) {
        const GameOutput *out = &outbox->items[i];
        (void)xQueueSend((QueueHandle_t)out->to, out, 0U);
    }
}

static bool GameShell_TakePinsetterRoll(GameShell *self, GameMessage *message)
{
    Pins pins;
    if (xQueueReceive(self->pinsetter, &pins, 0U) != pdPASS) {
        return false;
    }
    message->kind = GAME_MSG_PINSETTER_ROLL;
    message->pins = pins;
    return true;
}

/* The pinsetter's rolls before a command waiting with them. */
static bool GameShell_TakeMessage(GameShell *self, GameMessage *message)
{
    return GameShell_TakePinsetterRoll(self, message) ||
           (xQueueReceive(self->commands, message, 0U) == pdPASS);
}

static void GameShell_HandleEverythingWaiting(GameShell *self)
{
    while (GameShell_TakeMessage(self, &self->message)) {
        GameActor_Handle(&self->actor, &self->message, &self->outbox);
        GameShell_Deliver(&self->outbox);
    }
}

static void GameShell_Task(void *parameter)
{
    GameShell *self = (GameShell *)parameter;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        GameShell_HandleEverythingWaiting(self);
    }
}

void GameShell_Start(ScorerVariant variant, CountRule rule, UBaseType_t priority)
{
    GameShell *self = &s_shell;
    GameActor_Init(&self->actor, variant, rule);
    self->commands = xQueueCreateStatic(GAME_SHELL_COMMANDS, sizeof(GameMessage),
                                        self->commands_storage, &self->commands_queue);
    self->pinsetter = xQueueCreateStatic(GAME_SHELL_PINSETTER_ROLLS, sizeof(Pins),
                                         self->pinsetter_storage, &self->pinsetter_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    self->task = xTaskCreateStatic(&GameShell_Task, "game", configMINIMAL_STACK_SIZE, self,
                                   priority, self->stack, &self->task_buffer);
}

BaseType_t GameShell_Send(const GameMessage *message, TickType_t wait)
{
    const BaseType_t sent = xQueueSend(s_shell.commands, message, wait);
    if (sent == pdPASS) {
        xTaskNotifyGive(s_shell.task);
    }
    return sent;
}

void GameShell_PinsetterCountedFromIsr(Pins pins)
{
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(s_shell.pinsetter, &pins, &woken);
    vTaskNotifyGiveFromISR(s_shell.task, &woken);
    portYIELD_FROM_ISR(woken);
}
