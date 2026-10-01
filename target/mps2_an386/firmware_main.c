/* The firmware under QEMU, and its smoke test: the same actors as the XMC4500's (target/firmware),
 * with the pinsetters driven by a timer interrupt (pinsetter_timer.c), and a league task standing
 * in for whoever runs the league. It plays a game on each lane through the shell, reads the
 * results, drives the game task's worst case, and exits QEMU passing only if every result is
 * what the rules say. */

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "fault.h"
#include "firmware.h"
#include "pinsetter_timer.h"
#include "semihosting.h"

#define SMOKE_LEAGUE_ID 5U
#define SMOKE_LEAGUE_PRIORITY 1U
#define SMOKE_LEAGUE_MAILBOX 4U
#define SMOKE_PATIENCE pdMS_TO_TICKS(1000U)
#define SMOKE_ROLL_PERIOD_US 2000U

/* Lane 0 a perfect game, twelve strikes; lane 1 twenty-one fives, every frame a spare. */
#define SMOKE_LANE_0_ROLLS 12U
#define SMOKE_LANE_1_ROLLS 21U
#define SMOKE_GAME_ROLLS (SMOKE_LANE_0_ROLLS + SMOKE_LANE_1_ROLLS)
#define SMOKE_LANE_0_TOTAL 300U
#define SMOKE_LANE_1_TOTAL 150U
/* The worst case: thirteen pinsetter strikes, held behind a ninth-frame 7. */
#define SMOKE_HELD_ROLLS 13U

static const ScorerRules k_ten_pin = {10U, 2U, 10U, {2U, 1U, 0U}, 0U};

static PinsetterTimerRoll s_game_script[SMOKE_GAME_ROLLS];
static PinsetterTimerRoll s_held_script[SMOKE_HELD_ROLLS];
static QueueHandle_t s_mailbox;
static StaticQueue_t s_mailbox_queue;
static uint8_t s_mailbox_storage[SMOKE_LEAGUE_MAILBOX * sizeof(Message)];
static StaticTask_t s_league_task;
static StackType_t s_league_stack[512];
static RequestSeq s_seq;
static bool s_passed = true;

static Message Smoke_Request(Selector selector, ActorId from, ActorId to)
{
    Message message = {0};
    message.envelope.selector = selector;
    message.envelope.from = from;
    message.envelope.to = to;
    message.envelope.seq = ++s_seq;
    return message;
}

/* Sends `request` from the league and waits for its reply. */
static ReplyPayload Smoke_Ask(Message request)
{
    Message reply = {0};
    if ((ActorHost_Send(&request, SMOKE_PATIENCE) != pdPASS) ||
        (xQueueReceive(s_mailbox, &reply, SMOKE_PATIENCE) != pdPASS) ||
        (reply.envelope.seq != request.envelope.seq)) {
        Fault_Stop("smoke: no reply from the shell");
    }
    return reply.payload.reply;
}

static void Smoke_Check(const char *what, uint32_t actual, uint32_t expected)
{
    Semihosting_Write("SMOKE: ");
    Semihosting_Write(what);
    Semihosting_Write(": ");
    Semihosting_WriteNumber(actual);
    if (actual == expected) {
        Semihosting_Write("\n");
        return;
    }
    Semihosting_Write(", expected ");
    Semihosting_WriteNumber(expected);
    Semihosting_Write(": FAILED\n");
    s_passed = false;
}

static void Smoke_StartAGame(ActorId game)
{
    Message new_game = Smoke_Request(MSG_NEW_GAME, SMOKE_LEAGUE_ID, game);
    new_game.payload.new_game.rules = k_ten_pin;
    Smoke_Check("a new game's status", Smoke_Ask(new_game).status, REPLY_OK);
}

/* A subscription on the observer's behalf: its reply goes to the observer. */
static void Smoke_Subscribe(ActorId observer, ActorId game)
{
    const Message subscribe = Smoke_Request(MSG_SUBSCRIBE, observer, game);
    (void)ActorHost_Send(&subscribe, SMOKE_PATIENCE);
}

static uint32_t Smoke_FigureOf(ActorId actor)
{
    return Smoke_Ask(Smoke_Request(MSG_QUERY_FIGURE, SMOKE_LEAGUE_ID, actor)).score;
}

static void Smoke_RunTheTimer(const PinsetterTimerRoll *script, uint8_t count)
{
    PinsetterTimer_Start(script, count, SMOKE_ROLL_PERIOD_US);
    while (!PinsetterTimer_IsDone()) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    vTaskDelay(pdMS_TO_TICKS(50U)); /* the games take the last rolls */
}

/* A game on each lane, every ball from its pinsetter, interleaved: each lane's total, from its
 * game and from its scoreboard. */
static void Smoke_PlayBothLanes(void)
{
    Smoke_StartAGame(ACTOR_HOST_GAME_ID);
    Smoke_StartAGame(FIRMWARE_LANE_1_GAME_ID);
    Smoke_Subscribe(FIRMWARE_LANE_0_SCOREBOARD_ID, ACTOR_HOST_GAME_ID);
    Smoke_Subscribe(FIRMWARE_LANE_1_SCOREBOARD_ID, FIRMWARE_LANE_1_GAME_ID);
    uint8_t at = 0U;
    for (uint8_t ball = 0U; ball < SMOKE_LANE_1_ROLLS; ball++) {
        if (ball < SMOKE_LANE_0_ROLLS) {
            s_game_script[at++] = (PinsetterTimerRoll){0U, 10U};
        }
        s_game_script[at++] = (PinsetterTimerRoll){1U, 5U};
    }
    Smoke_RunTheTimer(s_game_script, SMOKE_GAME_ROLLS);
    Smoke_Check("lane 0's game", Smoke_FigureOf(ACTOR_HOST_GAME_ID), SMOKE_LANE_0_TOTAL);
    Smoke_Check("lane 0's scoreboard", Smoke_FigureOf(FIRMWARE_LANE_0_SCOREBOARD_ID),
                SMOKE_LANE_0_TOTAL);
    Smoke_Check("lane 1's game", Smoke_FigureOf(FIRMWARE_LANE_1_GAME_ID), SMOKE_LANE_1_TOTAL);
    Smoke_Check("lane 1's scoreboard", Smoke_FigureOf(FIRMWARE_LANE_1_SCOREBOARD_ID),
                SMOKE_LANE_1_TOTAL);
}

/* The game task's worst case, as the host's stack test drives it: on lane 0, told to both
 * scoreboards, an edit that reopens the nine complete frames and lets through held strikes that
 * complete all ten. Then its painted stack. */
static void Smoke_DriveTheWorstCase(void)
{
    Smoke_StartAGame(ACTOR_HOST_GAME_ID);
    for (uint8_t ball = 0U; ball < 11U; ball++) {
        Message roll = Smoke_Request(MSG_ROLL, SMOKE_LEAGUE_ID, ACTOR_HOST_GAME_ID);
        roll.payload.roll.pins = (ball < 10U) ? 10U : 7U;
        (void)Smoke_Ask(roll);
    }
    Smoke_Subscribe(FIRMWARE_LANE_1_SCOREBOARD_ID, ACTOR_HOST_GAME_ID);
    for (uint8_t ball = 0U; ball < SMOKE_HELD_ROLLS; ball++) {
        s_held_script[ball] = (PinsetterTimerRoll){0U, 10U};
    }
    Smoke_RunTheTimer(s_held_script, SMOKE_HELD_ROLLS);
    Message every_ball_out = Smoke_Request(MSG_EDIT, SMOKE_LEAGUE_ID, ACTOR_HOST_GAME_ID);
    every_ball_out.payload.edit.first_roll = 1U;
    every_ball_out.payload.edit.rolls_removed = 11U;
    Smoke_Check("the worst case's status", Smoke_Ask(every_ball_out).status, REPLY_OK);
    Smoke_Check("its total, twelve held strikes let through", Smoke_FigureOf(ACTOR_HOST_GAME_ID),
                SMOKE_LANE_0_TOTAL);
    const size_t used = ActorHost_TaskStackUsed();
    Semihosting_Write("SMOKE: the game task's painted stack: ");
    Semihosting_WriteNumber((uint32_t)used);
    Semihosting_Write(" bytes, of a budget of ");
    Semihosting_WriteNumber(ACTOR_HOST_TASK_STACK_BUDGET);
    Semihosting_Write("\n");
    s_passed = s_passed && (used <= ACTOR_HOST_TASK_STACK_BUDGET);
}

/* How many priority bits the interrupt controller implements, read back as the port reads them. */
static uint32_t Smoke_ImplementedPriorityBits(void)
{
    NVIC->IP[TIMER1_IRQn] = 0xFFU;
    uint32_t implemented = NVIC->IP[TIMER1_IRQn];
    NVIC->IP[TIMER1_IRQn] = 0U;
    uint32_t bits = 0U;
    while ((implemented & 0x80U) != 0U) {
        bits++;
        implemented = (implemented << 1U) & 0xFFU;
    }
    return bits;
}

/* The device header's __NVIC_PRIO_BITS, which configPRIO_BITS is, must be no more than the
 * hardware implements: each priority set is then its top bits, and the kernel's mask holds. QEMU
 * implements all eight, where ARM's AN386 board has the header's three. */
static void Smoke_CheckPriorityBits(void)
{
    const uint32_t implemented = Smoke_ImplementedPriorityBits();
    Semihosting_Write("SMOKE: the interrupt controller implements ");
    Semihosting_WriteNumber(implemented);
    Semihosting_Write(" priority bits; the device header says ");
    Semihosting_WriteNumber(__NVIC_PRIO_BITS);
    Semihosting_Write("\n");
    s_passed = s_passed && (__NVIC_PRIO_BITS <= implemented);
}

static void Smoke_League(void *parameter)
{
    (void)parameter;
    Smoke_CheckPriorityBits();
    Smoke_PlayBothLanes();
    Smoke_DriveTheWorstCase();
    Smoke_Check("outputs dropped", ActorHost_OutputsDropped(), 0U);
    Semihosting_Write(s_passed ? "SMOKE: PASSED\n" : "SMOKE: FAILED\n");
    Semihosting_Exit(s_passed);
}

int main(void)
{
    Firmware_HostTheLanes();
    s_mailbox = xQueueCreateStatic(SMOKE_LEAGUE_MAILBOX, sizeof(Message), s_mailbox_storage,
                                   &s_mailbox_queue);
    ActorHost_Bind(SMOKE_LEAGUE_ID, s_mailbox);
    /* FUNCTION POINTER EXEMPTION: FreeRTOS takes a task's entry function by address. */
    (void)xTaskCreateStatic(&Smoke_League, "league", 512U, NULL, SMOKE_LEAGUE_PRIORITY,
                            s_league_stack, &s_league_task);
    vTaskStartScheduler();
    Fault_Stop("mps2-an386: the scheduler returned");
}
