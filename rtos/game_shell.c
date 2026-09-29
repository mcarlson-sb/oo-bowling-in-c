#include "game_shell.h"

#include "task.h"

#define GAME_SHELL_COMMANDS 4U

typedef struct {
    GameActor actor;
    GameOutbox outbox;
    GameMessage message;
    QueueHandle_t commands;
    StaticQueue_t commands_queue;
    uint8_t commands_storage[GAME_SHELL_COMMANDS * sizeof(GameMessage)];
    StaticTask_t task;
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

static void GameShell_Task(void *parameter)
{
    GameShell *self = (GameShell *)parameter;
    for (;;) {
        if (xQueueReceive(self->commands, &self->message, portMAX_DELAY) == pdPASS) {
            GameActor_Handle(&self->actor, &self->message, &self->outbox);
            GameShell_Deliver(&self->outbox);
        }
    }
}

void GameShell_Start(ScorerVariant variant, CountRule rule, UBaseType_t priority)
{
    GameShell *self = &s_shell;
    GameActor_Init(&self->actor, variant, rule);
    self->commands = xQueueCreateStatic(GAME_SHELL_COMMANDS, sizeof(GameMessage),
                                        self->commands_storage, &self->commands_queue);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    (void)xTaskCreateStatic(&GameShell_Task, "game", configMINIMAL_STACK_SIZE, self, priority,
                            self->stack, &self->task);
}

BaseType_t GameShell_Send(const GameMessage *message, TickType_t wait)
{
    return xQueueSend(s_shell.commands, message, wait);
}
