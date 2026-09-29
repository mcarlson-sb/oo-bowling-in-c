/* The RTOS shell: the game actor in a task of its own, fed by queues, on FreeRTOS's POSIX port.
 * Each test starts the scheduler, so each runs in a process of its own. A client task does the
 * test's talking and ends the scheduler once it has what it needs; the test then reads what it
 * found, published through an atomic, since ThreadSanitizer can't see the port's signals order
 * anything. */

#include <gtest/gtest.h>

#include <atomic>

extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "game_shell.h"
}

namespace {

constexpr UBaseType_t kClientPriority = tskIDLE_PRIORITY + 1U;
constexpr UBaseType_t kGamePriority = tskIDLE_PRIORITY + 2U;
constexpr TickType_t kPatience = pdMS_TO_TICKS(1000);

/* A caller's own queue of outputs, for replies or as a subscriber. */
struct OutputQueue {
    static constexpr UBaseType_t kLength = 64U;
    StaticQueue_t queue;
    uint8_t storage[kLength * sizeof(GameOutput)];
    QueueHandle_t handle;

    void Create()
    {
        handle = xQueueCreateStatic(kLength, sizeof(GameOutput), storage, &queue);
    }
};

OutputQueue s_replies;

StaticTask_t s_client_task;
StackType_t s_client_stack[configMINIMAL_STACK_SIZE];
void (*s_client_body)();
std::atomic<bool> s_client_done{false};

void ClientTask(void *parameter)
{
    (void)parameter;
    s_client_body();
    s_client_done.store(true, std::memory_order_release);
    vTaskEndScheduler();
    for (;;) {
    }
}

/* Runs `body` in the client task, with the game shell started, and returns once the client has
 * finished and the scheduler has stopped. */
void RunClient(void (*body)())
{
    GameShell_Start(SCORER_TEN_PIN, SCORER_COUNT_PINS_DOWN, kGamePriority);
    s_replies.Create();
    s_client_body = body;
    ASSERT_NE(nullptr, xTaskCreateStatic(&ClientTask, "client", configMINIMAL_STACK_SIZE,
                                         nullptr, kClientPriority, s_client_stack,
                                         &s_client_task));
    vTaskStartScheduler();
    ASSERT_TRUE(s_client_done.load(std::memory_order_acquire));
}

GameMessage RollRequest(RequestSeq seq, Pins pins)
{
    GameMessage message = {};
    message.kind = GAME_MSG_ROLL;
    message.seq = seq;
    message.reply_to = s_replies.handle;
    message.pins = pins;
    return message;
}

BaseType_t s_sent;
BaseType_t s_received;
GameOutput s_reply;

} // namespace

TEST(GameShellTest, should_reply_on_the_callers_queue_to_a_roll_sent_to_the_game_task)
{
    RunClient([] {
        const GameMessage roll = RollRequest(7U, 3U);
        s_sent = GameShell_Send(&roll, 0U);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
    });
    ASSERT_EQ(pdPASS, s_sent);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OUT_REPLY, s_reply.kind);
    EXPECT_EQ(7U, s_reply.seq);
    EXPECT_EQ(GAME_OK, s_reply.status);
}
