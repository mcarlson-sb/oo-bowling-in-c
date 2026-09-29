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
    static constexpr UBaseType_t kMaxLength = 64U;
    StaticQueue_t queue;
    uint8_t storage[kMaxLength * sizeof(GameOutput)];
    QueueHandle_t handle;

    void Create(UBaseType_t length = kMaxLength)
    {
        handle = xQueueCreateStatic(length, sizeof(GameOutput), storage, &queue);
    }
};

OutputQueue s_replies;
OutputQueue s_subscriber;
OutputQueue s_second_subscriber;

/* The client's ids, bound to its queues. */
constexpr ActorId kClient = 2U;
constexpr ActorId kSubscriber = 3U;
constexpr ActorId kSecondSubscriber = 4U;
UBaseType_t s_subscriber_queue_length = OutputQueue::kMaxLength;

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
 * finished and the scheduler has stopped. A client above the game task can queue several
 * messages before the game task runs. */
void RunClient(void (*body)(), UBaseType_t client_priority = kClientPriority)
{
    GameShell_Start(SCORER_TEN_PIN, SCORER_COUNT_PINS_DOWN, kGamePriority);
    s_replies.Create();
    s_subscriber.Create(s_subscriber_queue_length);
    s_second_subscriber.Create();
    GameShell_Bind(kClient, s_replies.handle);
    GameShell_Bind(kSubscriber, s_subscriber.handle);
    GameShell_Bind(kSecondSubscriber, s_second_subscriber.handle);
    s_interrupt = xTaskCreateStatic(&InterruptTask, "interrupt", configMINIMAL_STACK_SIZE,
                                    nullptr, kInterruptPriority, s_interrupt_stack,
                                    &s_interrupt_task);
    ASSERT_NE(nullptr, s_interrupt);
    s_client_body = body;
    ASSERT_NE(nullptr, xTaskCreateStatic(&ClientTask, "client", configMINIMAL_STACK_SIZE,
                                         nullptr, client_priority, s_client_stack,
                                         &s_client_task));
    vTaskStartScheduler();
    ASSERT_TRUE(s_client_done.load(std::memory_order_acquire));
}

GameMessage RollRequest(RequestSeq seq, Pins pins)
{
    GameMessage message = {};
    message.envelope.selector = GAME_MSG_ROLL;
    message.envelope.seq = seq;
    message.envelope.from = kClient;
    message.envelope.to = GAME_SHELL_GAME_ID;
    message.payload.roll.pins = pins;
    return message;
}

GameMessage SubscribeRequest(RequestSeq seq)
{
    GameMessage message = {};
    message.envelope.selector = GAME_MSG_SUBSCRIBE;
    message.envelope.seq = seq;
    message.envelope.from = kSubscriber;
    message.envelope.to = GAME_SHELL_GAME_ID;
    return message;
}

GameMessage ScoreQuery(RequestSeq seq)
{
    GameMessage message = {};
    message.envelope.selector = GAME_MSG_QUERY_SCORE;
    message.envelope.seq = seq;
    message.envelope.from = kClient;
    message.envelope.to = GAME_SHELL_GAME_ID;
    return message;
}

GameMessage EditRequest(RequestSeq seq, RollNumber first, uint8_t removed,
                        std::initializer_list<Pins> new_pins)
{
    GameMessage message = {};
    message.envelope.selector = GAME_MSG_EDIT;
    message.envelope.seq = seq;
    message.envelope.from = kClient;
    message.envelope.to = GAME_SHELL_GAME_ID;
    message.payload.edit.first_roll = first;
    message.payload.edit.rolls_removed = removed;
    for (const Pins pins : new_pins) {
        message.payload.edit.new_pins[message.payload.edit.new_count++] = pins;
    }
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
    EXPECT_EQ(7U, s_reply.payload.reply.seq);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
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
    EXPECT_EQ(1U, s_event.payload.frame.frame_number);
    EXPECT_EQ(7U, s_event.payload.frame.frame_score);
    EXPECT_TRUE(s_event.payload.frame.frame_complete);
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
    EXPECT_EQ(1U, s_heard.back().payload.rolls_lost.lost);
}

namespace {

uint16_t s_dropped;

} // namespace

TEST(GameShellTest, should_drop_and_count_an_event_for_a_subscriber_whose_queue_is_full)
{
    /* The subscriber's queue has room for one, which its subscribe reply takes. */
    s_subscriber_queue_length = 1U;
    RunClient([] {
        const GameMessage subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        FirePinsetter({3U, 4U}); /* completes frame 1: an event it has no room for */
        const GameMessage query = ScoreQuery(2U);
        (void)GameShell_Send(&query, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience); /* after the rolls */
        s_dropped = GameShell_OutputsDropped();
    });
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(7U, s_reply.payload.reply.score);
    EXPECT_EQ(1U, s_dropped);
}

TEST(GameShellTest, should_play_the_pinsetters_waiting_rolls_before_an_edit_waiting_with_them)
{
    /* Above the game task, the client queues a correction of ball 1, and the interrupt then
     * counts two rolls, before the game task runs at all. Rolls first: the correction finds
     * ball 1 there, and 5 then 4 scores 9. Command first, it would find no ball 1. */
    RunClient(
        [] {
            const GameMessage correction = EditRequest(1U, 1U, 1U, {5U});
            s_sent = GameShell_Send(&correction, 0U);
            FirePinsetter({3U, 4U});
            s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        },
        kGamePriority + 1U);
    ASSERT_EQ(pdPASS, s_sent);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
    EXPECT_EQ(9U, s_reply.payload.reply.score);
}

namespace {

std::vector<GameOutput> s_heard_held;

} // namespace

TEST(GameShellTest, should_hold_a_miscounted_roll_until_a_correction_lets_it_through)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible: held, with the 3
     * after it. Correcting ball 1 to 2 lets both through: 2, 8, a spare, then 3, is 13. */
    RunClient([] {
        const GameMessage subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        GameOutput reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter({5U, 8U, 3U});
        s_heard_held = HearUntil(GAME_OUT_ROLL_HELD);
        const GameMessage correction = EditRequest(2U, 1U, 1U, {2U});
        (void)GameShell_Send(&correction, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_heard = HearUntil(GAME_OUT_FRAME_CHANGED);
    });
    ASSERT_FALSE(s_heard_held.empty());
    EXPECT_EQ(GAME_OUT_ROLL_HELD, s_heard_held.back().kind);
    EXPECT_EQ(8U, s_heard_held.back().payload.roll_held.pins);
    EXPECT_EQ(2U, s_heard_held.back().payload.roll_held.position);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
    EXPECT_EQ(13U, s_reply.payload.reply.score);
    ASSERT_FALSE(s_heard.empty());
    EXPECT_EQ(GAME_OUT_FRAME_CHANGED, s_heard.back().kind);
    EXPECT_EQ(1U, s_heard.back().payload.frame.frame_number);
    EXPECT_EQ(13U, s_heard.back().payload.frame.frame_score);
}

namespace {

size_t s_stack_used;

} // namespace

TEST(GameShellStackTest, should_keep_the_game_task_within_its_stack_budget_through_the_worst_case)
{
    /* The painted stack, the cross-check on the static call graph: the outbox's worst case, an
     * edit that reopens every frame and lets through held rolls that complete them all again,
     * told to two subscribers. */
    RunClient([] {
        for (int i = 0; i < 12; i++) {
            const GameMessage strike = RollRequest(static_cast<RequestSeq>(i + 1), 10U);
            (void)GameShell_Send(&strike, kPatience);
            (void)xQueueReceive(s_replies.handle, &s_reply, kPatience);
        }
        GameMessage subscribe = SubscribeRequest(20U);
        (void)GameShell_Send(&subscribe, kPatience);
        subscribe.envelope.from = kSecondSubscriber;
        (void)GameShell_Send(&subscribe, kPatience);
        FirePinsetter(13, 10U); /* held: the game is over */
        const GameMessage every_ball_out = EditRequest(21U, 1U, 12U, {});
        (void)GameShell_Send(&every_ball_out, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_stack_used = GameShell_TaskStackUsed();
    });
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
    EXPECT_LE(s_stack_used, static_cast<size_t>(GAME_SHELL_TASK_STACK_BUDGET));
}

TEST(GameShellTest, should_drop_and_count_an_output_to_an_id_nothing_is_bound_to)
{
    /* A subscriber at id 5, which no queue is bound to: its subscribe reply has nowhere to go.
     * The game carries on, and the client's own reply still arrives. */
    RunClient([] {
        GameMessage subscribe = SubscribeRequest(1U);
        subscribe.envelope.from = 5U;
        (void)GameShell_Send(&subscribe, kPatience);
        const GameMessage query = ScoreQuery(2U);
        (void)GameShell_Send(&query, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_dropped = GameShell_OutputsDropped();
    });
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(2U, s_reply.payload.reply.seq);
    EXPECT_EQ(1U, s_dropped);
}
