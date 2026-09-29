/* The RTOS shell: the game actor in a task of its own, fed by queues, on FreeRTOS's POSIX port.
 * Each test starts the scheduler, so each runs in a process of its own. A client task does the
 * test's talking and ends the scheduler once it has what it needs; the test then reads what it
 * found, published through an atomic, since ThreadSanitizer can't see the port's signals order
 * anything. */

#include <gtest/gtest.h>

#include <atomic>
#include <initializer_list>
#include <vector>

extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "game_shell.h"
}

namespace {

constexpr UBaseType_t kClientPriority = tskIDLE_PRIORITY + 1U;
constexpr UBaseType_t kGamePriority = tskIDLE_PRIORITY + 2U;
constexpr UBaseType_t kInterruptPriority = configMAX_PRIORITIES - 1U;
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
OutputQueue s_subscriber;

/* The pinsetter's interrupt, simulated by the highest-priority task: when fired, it counts its
 * rolls one after another, as back-to-back interrupts would, and nothing lower runs until it's
 * done. Unlike a real interrupt, it runs only when the kernel schedules it, never in the middle
 * of another task's instruction. */
StaticTask_t s_interrupt_task;
StackType_t s_interrupt_stack[configMINIMAL_STACK_SIZE];
TaskHandle_t s_interrupt;
std::vector<Pins> s_interrupt_rolls;

void InterruptTask(void *parameter)
{
    (void)parameter;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (const Pins pins : s_interrupt_rolls) {
            GameShell_PinsetterCountedFromIsr(pins);
        }
    }
}

void FirePinsetter(std::initializer_list<Pins> rolls)
{
    s_interrupt_rolls = rolls;
    xTaskNotifyGive(s_interrupt);
}

void FirePinsetter(int count, Pins pins)
{
    s_interrupt_rolls.assign(static_cast<size_t>(count), pins);
    xTaskNotifyGive(s_interrupt);
}

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
    s_subscriber.Create();
    s_interrupt = xTaskCreateStatic(&InterruptTask, "interrupt", configMINIMAL_STACK_SIZE,
                                    nullptr, kInterruptPriority, s_interrupt_stack,
                                    &s_interrupt_task);
    ASSERT_NE(nullptr, s_interrupt);
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

GameMessage SubscribeRequest(RequestSeq seq)
{
    GameMessage message = {};
    message.kind = GAME_MSG_SUBSCRIBE;
    message.seq = seq;
    message.reply_to = s_subscriber.handle;
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

namespace {

GameOutput s_event;

} // namespace

TEST(GameShellTest, should_tell_a_subscriber_the_frame_the_pinsetters_rolls_complete)
{
    RunClient([] {
        const GameMessage subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        GameOutput reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter({3U, 4U});
        s_received = xQueueReceive(s_subscriber.handle, &s_event, kPatience);
    });
    ASSERT_EQ(pdPASS, s_sent);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OUT_FRAME_CHANGED, s_event.kind);
    EXPECT_EQ(1U, s_event.frame.frame_number);
    EXPECT_EQ(7U, s_event.frame.frame_score);
    EXPECT_TRUE(s_event.frame.frame_complete);
}

namespace {

/* Every output the subscriber hears, until it has heard `kind` or gone quiet for kPatience. */
std::vector<GameOutput> HearUntil(GameOutputKind kind)
{
    std::vector<GameOutput> heard;
    GameOutput out;
    while (xQueueReceive(s_subscriber.handle, &out, kPatience) == pdPASS) {
        heard.push_back(out);
        if (out.kind == kind) {
            break;
        }
    }
    return heard;
}

std::vector<GameOutput> s_heard;

} // namespace

TEST(GameShellTest, should_count_a_roll_lost_to_the_pinsetters_full_queue_and_tell_the_subscriber)
{
    /* The interrupt outruns the game task: 33 rolls into a queue of 32. */
    RunClient([] {
        const GameMessage subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        GameOutput reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter(33, 0U);
        s_heard = HearUntil(GAME_OUT_ROLLS_LOST);
    });
    ASSERT_FALSE(s_heard.empty());
    EXPECT_EQ(GAME_OUT_ROLLS_LOST, s_heard.back().kind);
    EXPECT_EQ(1U, s_heard.back().lost);
}
