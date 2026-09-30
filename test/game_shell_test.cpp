/* The RTOS shell: the game actor in a task of its own, fed by queues, on FreeRTOS's POSIX port.
 * Each test starts the scheduler, so each runs in a process of its own. A client task does the
 * test's talking and ends the scheduler once it has what it needs; the test then reads what it
 * found, published through an atomic, since ThreadSanitizer can't see the port's signals order
 * anything. */

#include <gtest/gtest.h>

#include <atomic>
#include <initializer_list>
#include <utility>
#include <vector>

extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "game_shell.h"
}

#include "rules_presets.h"

namespace {

constexpr UBaseType_t kClientPriority = tskIDLE_PRIORITY + 1U;
constexpr UBaseType_t kGamePriority = tskIDLE_PRIORITY + 2U;
constexpr UBaseType_t kInterruptPriority = configMAX_PRIORITIES - 1U;
/* Above the game: a hosted observer takes each event as the game sends it. */
constexpr UBaseType_t kObserverPriority = kGamePriority + 1U;
constexpr TickType_t kPatience = pdMS_TO_TICKS(1000);

/* A caller's own queue of outputs, for replies or as a subscriber. */
struct OutputQueue {
    static constexpr UBaseType_t kMaxLength = 64U;
    StaticQueue_t queue;
    uint8_t storage[kMaxLength * sizeof(Message)];
    QueueHandle_t handle;

    void Create(UBaseType_t length = kMaxLength)
    {
        handle = xQueueCreateStatic(length, sizeof(Message), storage, &queue);
    }
};

OutputQueue s_replies;
OutputQueue s_subscriber;
OutputQueue s_second_subscriber;

/* The client's ids, bound to its queues. */
constexpr ActorId kClient = 2U;
constexpr ActorId kSubscriber = 3U;
constexpr ActorId kSecondSubscriber = 4U;
/* The second lane's game, hosted only by the tests that ask for it. */
constexpr ActorId kSecondLane = 6U;
bool s_second_lane = false;
UBaseType_t s_subscriber_queue_length = OutputQueue::kMaxLength;
/* What sits at kSubscriber and kSecondSubscriber: external queues the test reads, unless a test
 * hosts a kind there. */
ActorKind s_subscriber_kind = ACTOR_KIND_EXTERNAL;
ActorKind s_second_subscriber_kind = ACTOR_KIND_EXTERNAL;
/* More hosted observers, for the tests that ask for them: an id and its kind each. */
std::vector<std::pair<ActorId, ActorKind>> s_more_observers;
UBaseType_t s_observer_priority = kObserverPriority;

void BindOrHost(ActorId id, ActorKind kind, QueueHandle_t queue)
{
    if (kind == ACTOR_KIND_SCOREBOARD) {
        GameShell_HostScoreboard(id);
    } else if (kind == ACTOR_KIND_RUNNING_AVERAGE) {
        GameShell_HostRunningAverage(id);
    } else {
        GameShell_Bind(id, queue);
    }
}

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
std::atomic<bool> s_new_game_failed{false};

/* Every test's game: ten-pin, started by the client before its body runs. */
void StartATenPinGame()
{
    Message new_game = {};
    new_game.envelope.selector = MSG_NEW_GAME;
    new_game.envelope.from = kClient;
    new_game.envelope.to = GAME_SHELL_GAME_ID;
    new_game.payload.new_game.rules = rules::kTenPin;
    Message reply;
    if ((GameShell_Send(&new_game, kPatience) != pdPASS) ||
        (xQueueReceive(s_replies.handle, &reply, kPatience) != pdPASS) ||
        (reply.payload.reply.status != GAME_OK)) {
        s_new_game_failed.store(true, std::memory_order_release);
    }
}

void ClientTask(void *parameter)
{
    (void)parameter;
    StartATenPinGame();
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
    GameShell_Start(kGamePriority, s_observer_priority);
    s_replies.Create();
    s_subscriber.Create(s_subscriber_queue_length);
    s_second_subscriber.Create();
    GameShell_Bind(kClient, s_replies.handle);
    if (s_second_lane) {
        GameShell_HostGame(kSecondLane, kGamePriority);
    }
    BindOrHost(kSubscriber, s_subscriber_kind, s_subscriber.handle);
    BindOrHost(kSecondSubscriber, s_second_subscriber_kind, s_second_subscriber.handle);
    for (const auto &observer : s_more_observers) {
        BindOrHost(observer.first, observer.second, nullptr);
    }
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
    ASSERT_FALSE(s_new_game_failed.load(std::memory_order_acquire)) << "setup: the new game";
}

Message RollRequest(RequestSeq seq, Pins pins)
{
    Message message = {};
    message.envelope.selector = MSG_ROLL;
    message.envelope.seq = seq;
    message.envelope.from = kClient;
    message.envelope.to = GAME_SHELL_GAME_ID;
    message.payload.roll.pins = pins;
    return message;
}

Message SubscribeRequest(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_SUBSCRIBE;
    message.envelope.seq = seq;
    message.envelope.from = kSubscriber;
    message.envelope.to = GAME_SHELL_GAME_ID;
    return message;
}

Message ScoreQuery(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_SCORE;
    message.envelope.seq = seq;
    message.envelope.from = kClient;
    message.envelope.to = GAME_SHELL_GAME_ID;
    return message;
}

Message EditRequest(RequestSeq seq, RollNumber first, uint8_t removed,
                        std::initializer_list<Pins> new_pins)
{
    Message message = {};
    message.envelope.selector = MSG_EDIT;
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
Message s_reply;

} // namespace

TEST(GameShellTest, should_reply_on_the_callers_queue_to_a_roll_sent_to_the_game_task)
{
    RunClient([] {
        const Message roll = RollRequest(7U, 3U);
        s_sent = GameShell_Send(&roll, 0U);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
    });
    ASSERT_EQ(pdPASS, s_sent);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(MSG_REPLY, s_reply.envelope.selector);
    EXPECT_EQ(7U, s_reply.envelope.seq);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
}

namespace {

Message s_event;

} // namespace

TEST(GameShellTest, should_tell_a_subscriber_the_frame_the_pinsetters_rolls_complete)
{
    RunClient([] {
        const Message subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        Message reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter({3U, 4U});
        s_received = xQueueReceive(s_subscriber.handle, &s_event, kPatience);
    });
    ASSERT_EQ(pdPASS, s_sent);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(MSG_FRAME_CHANGED, s_event.envelope.selector);
    EXPECT_EQ(1U, s_event.payload.frame.frame_number);
    EXPECT_EQ(7U, s_event.payload.frame.frame_score);
    EXPECT_TRUE(s_event.payload.frame.frame_complete);
}

namespace {

/* Every output the subscriber hears, until it has heard `kind` or gone quiet for kPatience. */
std::vector<Message> HearUntil(Selector kind)
{
    std::vector<Message> heard;
    Message out;
    while (xQueueReceive(s_subscriber.handle, &out, kPatience) == pdPASS) {
        heard.push_back(out);
        if (out.envelope.selector == kind) {
            break;
        }
    }
    return heard;
}

std::vector<Message> s_heard;

} // namespace

TEST(GameShellTest, should_count_a_roll_lost_to_the_pinsetters_full_queue_and_tell_the_subscriber)
{
    /* The interrupt outruns the game task: 33 rolls into a queue of 32. */
    RunClient([] {
        const Message subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        Message reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter(33, 0U);
        s_heard = HearUntil(MSG_ROLLS_LOST);
    });
    ASSERT_FALSE(s_heard.empty());
    EXPECT_EQ(MSG_ROLLS_LOST, s_heard.back().envelope.selector);
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
        const Message subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        FirePinsetter({3U, 4U}); /* completes frame 1: an event it has no room for */
        const Message query = ScoreQuery(2U);
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
            const Message correction = EditRequest(1U, 1U, 1U, {5U});
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

std::vector<Message> s_heard_held;

} // namespace

TEST(GameShellTest, should_hold_a_miscounted_roll_until_a_correction_lets_it_through)
{
    /* The pinsetter counted 5 when 2 fell, so its true 8 looks impossible: held, with the 3
     * after it. Correcting ball 1 to 2 lets both through: 2, 8, a spare, then 3, is 13. */
    RunClient([] {
        const Message subscribe = SubscribeRequest(1U);
        s_sent = GameShell_Send(&subscribe, 0U);
        Message reply;
        (void)xQueueReceive(s_subscriber.handle, &reply, kPatience);
        FirePinsetter({5U, 8U, 3U});
        s_heard_held = HearUntil(MSG_ROLL_HELD);
        const Message correction = EditRequest(2U, 1U, 1U, {2U});
        (void)GameShell_Send(&correction, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_heard = HearUntil(MSG_FRAME_CHANGED);
    });
    ASSERT_FALSE(s_heard_held.empty());
    EXPECT_EQ(MSG_ROLL_HELD, s_heard_held.back().envelope.selector);
    EXPECT_EQ(8U, s_heard_held.back().payload.roll_held.pins);
    EXPECT_EQ(2U, s_heard_held.back().payload.roll_held.position);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(GAME_OK, s_reply.payload.reply.status);
    EXPECT_EQ(13U, s_reply.payload.reply.score);
    ASSERT_FALSE(s_heard.empty());
    EXPECT_EQ(MSG_FRAME_CHANGED, s_heard.back().envelope.selector);
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
            const Message strike = RollRequest(static_cast<RequestSeq>(i + 1), 10U);
            (void)GameShell_Send(&strike, kPatience);
            (void)xQueueReceive(s_replies.handle, &s_reply, kPatience);
        }
        Message subscribe = SubscribeRequest(20U);
        (void)GameShell_Send(&subscribe, kPatience);
        subscribe.envelope.from = kSecondSubscriber;
        (void)GameShell_Send(&subscribe, kPatience);
        FirePinsetter(13, 10U); /* held: the game is over */
        const Message every_ball_out = EditRequest(21U, 1U, 12U, {});
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
        Message subscribe = SubscribeRequest(1U);
        subscribe.envelope.from = 5U;
        (void)GameShell_Send(&subscribe, kPatience);
        const Message query = ScoreQuery(2U);
        (void)GameShell_Send(&query, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_dropped = GameShell_OutputsDropped();
    });
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(2U, s_reply.envelope.seq);
    EXPECT_EQ(1U, s_dropped);
}

TEST(GameShellTest, should_drop_and_count_an_output_to_an_id_past_the_routing_table)
{
    RunClient([] {
        Message subscribe = SubscribeRequest(1U);
        subscribe.envelope.from = 200U;
        (void)GameShell_Send(&subscribe, kPatience);
        const Message query = ScoreQuery(2U);
        (void)GameShell_Send(&query, kPatience);
        s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
        s_dropped = GameShell_OutputsDropped();
    });
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(1U, s_dropped);
}

/* ---- Rebinding: the same game, the same sender, whoever sits at the subscriber's id ------- */

namespace {

Message QueryTo(ActorId to, RequestSeq seq)
{
    Message message = ScoreQuery(seq);
    message.envelope.to = to;
    return message;
}

uint16_t s_dropped_after;

/* The one scenario every binding plays, sent by the client: the subscriber's id subscribed, a
 * spare and an open frame rolled (7, then 12), and whoever sits at that id asked QUERY_SCORE. The
 * game's code and this code are the same every time; only the binding differs. */
void SubscribeRollAndAsk()
{
    const Message subscribe = SubscribeRequest(1U);
    (void)GameShell_Send(&subscribe, kPatience);
    RequestSeq seq = 2U;
    for (const Pins pins : std::initializer_list<Pins>{3U, 4U, 5U, 5U, 2U}) {
        const Message roll = RollRequest(seq++, pins);
        (void)GameShell_Send(&roll, kPatience);
        (void)xQueueReceive(s_replies.handle, &s_reply, kPatience);
    }
    const Message ask = QueryTo(kSubscriber, seq);
    (void)GameShell_Send(&ask, kPatience);
}

/* A kind hosted at the subscriber's id answers the client. */
void SubscribeRollAndAskTheSubscriber()
{
    SubscribeRollAndAsk();
    s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
    s_dropped_after = GameShell_OutputsDropped();
}

} // namespace

TEST(GameShellRebindingTest, should_answer_the_total_when_a_scoreboard_sits_at_the_subscriber_id)
{
    s_subscriber_kind = ACTOR_KIND_SCOREBOARD;
    RunClient(&SubscribeRollAndAskTheSubscriber);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(MSG_REPLY, s_reply.envelope.selector);
    EXPECT_EQ(kSubscriber, s_reply.envelope.from);
    EXPECT_EQ(19U, s_reply.payload.reply.score); /* 7 + 12 */
    EXPECT_EQ(0U, s_dropped_after);
}

TEST(GameShellRebindingTest, should_answer_the_average_when_a_running_average_sits_there)
{
    s_subscriber_kind = ACTOR_KIND_RUNNING_AVERAGE;
    RunClient(&SubscribeRollAndAskTheSubscriber);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(kSubscriber, s_reply.envelope.from);
    EXPECT_EQ(9U, s_reply.payload.reply.score); /* 19 over 2 frames, rounded down */
    EXPECT_EQ(0U, s_dropped_after);
}

namespace {

std::vector<Message> s_recorded;

/* A recording double answers no one: the test reads what reached it, the question last. Waiting
 * for a reply that can't come would only wait out kPatience, which ThreadSanitizer's slow ticks
 * stretch past the test's timeout. */
void SubscribeRollAskAndReadTheRecording()
{
    SubscribeRollAndAsk();
    Message message;
    while (xQueueReceive(s_subscriber.handle, &message, kPatience) == pdPASS) {
        s_recorded.push_back(message);
        if (message.envelope.selector == MSG_QUERY_SCORE) {
            break;
        }
    }
    s_received = xQueueReceive(s_replies.handle, &s_reply, 0U);
}

} // namespace

TEST(GameShellRebindingTest, should_reach_a_recording_double_that_sits_there_with_the_same_messages)
{
    /* An external queue the test reads: it records the game's messages, and the question, which
     * nothing there answers. */
    s_subscriber_kind = ACTOR_KIND_EXTERNAL;
    RunClient(&SubscribeRollAskAndReadTheRecording);
    EXPECT_NE(pdPASS, s_received); /* no one answered the client */
    ASSERT_EQ(4U, s_recorded.size());
    EXPECT_EQ(MSG_REPLY, s_recorded[0].envelope.selector); /* to its subscription */
    EXPECT_EQ(MSG_FRAME_CHANGED, s_recorded[1].envelope.selector);
    EXPECT_EQ(7U, s_recorded[1].payload.frame.frame_score);
    EXPECT_EQ(MSG_FRAME_CHANGED, s_recorded[2].envelope.selector);
    EXPECT_EQ(12U, s_recorded[2].payload.frame.frame_score);
    EXPECT_EQ(MSG_QUERY_SCORE, s_recorded[3].envelope.selector);
}

/* ---- The observers outrank the game: why their mailbox of 4 takes its bursts ---------------- */

namespace {

/* The game's worst case, as the stack test plays it: 43 messages sent for one edit, 21 of them to
 * each subscriber, with no wait. */
void PlayTheGamesWorstBurst()
{
    for (int i = 0; i < 12; i++) {
        const Message strike = RollRequest(static_cast<RequestSeq>(i + 1), 10U);
        (void)GameShell_Send(&strike, kPatience);
        (void)xQueueReceive(s_replies.handle, &s_reply, kPatience);
    }
    Message subscribe = SubscribeRequest(20U);
    (void)GameShell_Send(&subscribe, kPatience);
    subscribe.envelope.from = kSecondSubscriber;
    (void)GameShell_Send(&subscribe, kPatience);
    FirePinsetter(13, 10U);
    const Message every_ball_out = EditRequest(21U, 1U, 12U, {});
    (void)GameShell_Send(&every_ball_out, kPatience);
    s_received = xQueueReceive(s_replies.handle, &s_reply, kPatience);
    s_dropped_after = GameShell_OutputsDropped();
}

} // namespace

TEST(GameShellObserverTest, should_drop_nothing_of_the_games_worst_burst_to_two_hosted_observers)
{
    /* The observers' task outranks the game's, so it takes each event as the game posts it, and
     * their shared mailbox of 4 never fills. */
    s_subscriber_kind = ACTOR_KIND_SCOREBOARD;
    s_second_subscriber_kind = ACTOR_KIND_RUNNING_AVERAGE;
    RunClient(&PlayTheGamesWorstBurst);
    ASSERT_EQ(pdPASS, s_received);
    EXPECT_EQ(0U, s_dropped_after);
}

TEST(GameShellObserverDeathTest, should_refuse_to_start_observers_that_do_not_outrank_the_game)
{
    /* The observers' mailbox of 4 takes a game's bursts only because they preempt the game after
     * every post (the test above). Level with it or below it, their backlog is bounded only by how
     * long the game runs, so the shell stops rather than start that way. */
    EXPECT_DEATH(GameShell_Start(kGamePriority, kGamePriority), "must outrank every game");
}

/* ---- Two lanes: two games, each in a task of its own ----------------------------------------- */

namespace {

Message ToLane(ActorId lane, Message message)
{
    message.envelope.to = lane;
    return message;
}

Message NewGameAt(ActorId lane, RequestSeq seq, const ScorerRules &rules)
{
    Message message = {};
    message.envelope.selector = MSG_NEW_GAME;
    message.envelope.from = kClient;
    message.envelope.to = lane;
    message.envelope.seq = seq;
    message.payload.new_game.rules = rules;
    return message;
}

/* Sends to a lane and waits for its reply. */
Message Ask(const Message &message)
{
    Message reply = {};
    (void)GameShell_Send(&message, kPatience);
    (void)xQueueReceive(s_replies.handle, &reply, kPatience);
    return reply;
}

Message s_second_lane_new_game;
Message s_first_lane_last;
Message s_second_lane_last;

} // namespace

TEST(GameShellLanesTest, should_play_two_lanes_each_by_its_own_rules_in_tasks_of_their_own)
{
    /* Lane 1 was started as ten-pin by every test's client; lane 2 is started as candlepin. */
    s_second_lane = true;
    RunClient([] {
        s_second_lane_new_game = Ask(NewGameAt(kSecondLane, 1U, rules::kCandlepin));
        (void)Ask(ToLane(kSecondLane, RollRequest(2U, 3U)));
        (void)Ask(ToLane(kSecondLane, RollRequest(3U, 3U)));
        s_second_lane_last = Ask(ToLane(kSecondLane, RollRequest(4U, 3U)));
        (void)Ask(RollRequest(5U, 3U));
        s_first_lane_last = Ask(RollRequest(6U, 4U));
    });
    EXPECT_EQ(GAME_OK, s_second_lane_new_game.payload.reply.status);
    EXPECT_EQ(kSecondLane, s_second_lane_last.envelope.from);
    EXPECT_EQ(9U, s_second_lane_last.payload.reply.score); /* candlepin: 3, 3, 3 is a frame */
    EXPECT_EQ(GAME_SHELL_GAME_ID, s_first_lane_last.envelope.from);
    EXPECT_EQ(7U, s_first_lane_last.payload.reply.score); /* ten-pin: 3, 4 is a frame */
}

TEST(GameShellObserverDeathTest, should_refuse_a_second_lane_the_observers_do_not_outrank)
{
    /* Every game, not only the first: the observers must preempt each of them. */
    EXPECT_DEATH(
        {
            GameShell_Start(kGamePriority, kObserverPriority);
            GameShell_HostGame(kSecondLane, kObserverPriority);
        },
        "must outrank every game");
}

namespace {

Message SubscribeTo(ActorId lane, ActorId subscriber, RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_SUBSCRIBE;
    message.envelope.from = subscriber;
    message.envelope.to = lane;
    message.envelope.seq = seq;
    return message;
}

/* Lane 2's own scoreboard and running average. */
constexpr ActorId kSecondLaneBoard = 7U;
constexpr ActorId kSecondLaneAverage = 8U;

Message s_lane_answers[4];

/* A scoreboard and a running average per lane, each subscribed to its own: lane 1's 3, 4 then
 * 5, 2 (7 and 7), and lane 2's 3, 3, 3 (9). Each observer is then asked QUERY_SCORE. */
void AScoreboardAndAnAveragePerLane()
{
    RequestSeq seq = 1U;
    (void)Ask(NewGameAt(kSecondLane, seq++, rules::kCandlepin));
    const Message subscriptions[] = {SubscribeTo(GAME_SHELL_GAME_ID, kSubscriber, seq++),
                                     SubscribeTo(GAME_SHELL_GAME_ID, kSecondSubscriber, seq++),
                                     SubscribeTo(kSecondLane, kSecondLaneBoard, seq++),
                                     SubscribeTo(kSecondLane, kSecondLaneAverage, seq++)};
    for (const Message &subscription : subscriptions) {
        (void)GameShell_Send(&subscription, kPatience);
    }
    for (const Pins pins : std::initializer_list<Pins>{3U, 4U, 5U, 2U}) {
        (void)Ask(RollRequest(seq++, pins));
    }
    for (int ball = 0; ball < 3; ball++) {
        (void)Ask(ToLane(kSecondLane, RollRequest(seq++, 3U)));
    }
    const ActorId observers[] = {kSubscriber, kSecondSubscriber, kSecondLaneBoard,
                                 kSecondLaneAverage};
    for (int i = 0; i < 4; i++) {
        s_lane_answers[i] = Ask(QueryTo(observers[i], seq++));
    }
}

} // namespace

TEST(GameShellLanesTest, should_keep_each_lanes_frames_apart_with_an_observer_instance_per_lane)
{
    /* One scoreboard of both lanes answered 9, not 16: lane 2's frame 1 overwrote lane 1's,
     * as predicted. An instance per lane keeps them apart, and the observers are unchanged. */
    s_second_lane = true;
    s_subscriber_kind = ACTOR_KIND_SCOREBOARD;
    s_second_subscriber_kind = ACTOR_KIND_RUNNING_AVERAGE;
    s_more_observers = {{kSecondLaneBoard, ACTOR_KIND_SCOREBOARD},
                        {kSecondLaneAverage, ACTOR_KIND_RUNNING_AVERAGE}};
    RunClient(&AScoreboardAndAnAveragePerLane);
    EXPECT_EQ(14U, s_lane_answers[0].payload.reply.score); /* lane 1's total: 7 + 7 */
    EXPECT_EQ(7U, s_lane_answers[1].payload.reply.score);  /* lane 1's average */
    EXPECT_EQ(9U, s_lane_answers[2].payload.reply.score);  /* lane 2's total */
    EXPECT_EQ(9U, s_lane_answers[3].payload.reply.score);  /* lane 2's average */
}

namespace {

Message s_lane_stats[2];

/* Each lane's running average asked for its statistics, after the same play. */
void EachLanesAverageAskedForItsStatistics()
{
    AScoreboardAndAnAveragePerLane();
    RequestSeq seq = 100U;
    const ActorId averages[] = {kSecondSubscriber, kSecondLaneAverage};
    for (int i = 0; i < 2; i++) {
        Message ask = QueryTo(averages[i], seq++);
        ask.envelope.selector = MSG_QUERY_STATS;
        s_lane_stats[i] = Ask(ask);
    }
}

} // namespace

TEST(GameShellLanesTest, should_combine_two_lanes_statistics_into_the_exact_average_of_both)
{
    /* Per lane, the averages round: 7 and 9, whose own average would be 8. From the facts
     * behind them, both lanes' totals and complete frames, the average of both is exact and rounds
     * once: 23 over 3 frames, 7. */
    s_second_lane = true;
    s_subscriber_kind = ACTOR_KIND_SCOREBOARD;
    s_second_subscriber_kind = ACTOR_KIND_RUNNING_AVERAGE;
    s_more_observers = {{kSecondLaneBoard, ACTOR_KIND_SCOREBOARD},
                        {kSecondLaneAverage, ACTOR_KIND_RUNNING_AVERAGE}};
    RunClient(&EachLanesAverageAskedForItsStatistics);
    const StatsPayload &lane1 = s_lane_stats[0].payload.stats;
    const StatsPayload &lane2 = s_lane_stats[1].payload.stats;
    ASSERT_EQ(MSG_STATS, s_lane_stats[0].envelope.selector);
    ASSERT_EQ(MSG_STATS, s_lane_stats[1].envelope.selector);
    EXPECT_EQ(14U, lane1.total);
    EXPECT_EQ(2U, lane1.complete_frames);
    EXPECT_EQ(9U, lane2.total);
    EXPECT_EQ(1U, lane2.complete_frames);
    const unsigned total = static_cast<unsigned>(lane1.total) + lane2.total;
    const unsigned frames = static_cast<unsigned>(lane1.complete_frames) + lane2.complete_frames;
    const unsigned both = total / frames;
    EXPECT_EQ(7U, both);
    EXPECT_NE(both, (7U + 9U) / 2U); /* not the average of the rounded averages */
}
