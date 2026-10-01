/* An early timing estimate, on QEMU: how many instructions the game's worst-case message takes,
 * an edit that reopens every frame and lets twelve held strikes through, told to two
 * subscribers, and how many a QUERY_FIGURE takes. Run with -icount shift=0, QEMU's virtual clock
 * advances one nanosecond an instruction, so the CMSDK timer 1, counting at 25 MHz, counts one
 * tick per 40 instructions; a block of NOPs calibrates that. Each message runs many times, from
 * the same state, to average the tick's granularity away. An instruction count, not a cycle
 * count: converted at 120 MHz with a stated CPI range, it is an estimate, for the hardware stage's
 * DWT cycle counter to replace. */

#include <stdint.h>

#include "device.h"
#include "game_actor.h"
#include "game_actor_state.h"
#include "outbox.h"
#include "scoreboard.h"
#include "scoreboard_state.h"
#include "semihosting.h"

#define TIMING_TIMER_BASE 0x40001000UL
#define TIMING_TIMER_CTRL (*(volatile uint32_t *)(TIMING_TIMER_BASE + 0x00U))
#define TIMING_TIMER_VALUE (*(volatile uint32_t *)(TIMING_TIMER_BASE + 0x04U))
#define TIMING_TIMER_RELOAD (*(volatile uint32_t *)(TIMING_TIMER_BASE + 0x08U))
#define TIMING_INSTRUCTIONS_PER_TICK 40U /* 1 ns an instruction, 40 ns a tick at 25 MHz */
#define TIMING_EDIT_RUNS 200U
#define TIMING_QUERY_RUNS 2000U

#define TIMING_GAME_ID 1U
#define TIMING_FIRST_SCOREBOARD_ID 3U
#define TIMING_SECOND_SCOREBOARD_ID 4U
#define TIMING_CLIENT_ID 5U
/* Clocks per instruction, the range the estimate states: 1.0 at best, every instruction from the
 * XMC4500's cache in one cycle; 2.0 for flash wait states, branches and loads past it. */
#define TIMING_CPI_TENTHS_LOW 10U
#define TIMING_CPI_TENTHS_HIGH 20U
#define TIMING_CPU_MHZ 120U

static const ScorerRules k_ten_pin = {10U, 2U, 10U, {2U, 1U, 0U}, 0U};

static GameActor s_game;
static GameActor s_game_before;
static Scoreboard s_scoreboards[2];
static Scoreboard s_scoreboards_before[2];
static Message s_game_sent[GAME_OUTBOX_CAPACITY];
static Message s_scoreboard_sent[SCOREBOARD_MOST_SENT];
static Outbox s_game_outbox;
static Outbox s_scoreboard_outbox;

static uint32_t Timing_Now(void)
{
    return TIMING_TIMER_VALUE; /* counts down */
}

static Message Timing_Message(Selector selector, ActorId from)
{
    Message message = {0};
    message.envelope.selector = selector;
    message.envelope.from = from;
    message.envelope.to = TIMING_GAME_ID;
    message.envelope.seq = 1U;
    return message;
}

static void Timing_Tell(Message message)
{
    s_game_outbox.count = 0U;
    GameActor_Handle(&s_game, &message, &s_game_outbox);
}

/* The host's worst case: ten strikes and a 7 rolled, two scoreboards subscribed, and thirteen
 * pinsetter strikes held behind the 7, which an edit taking out the first eleven balls lets
 * through, all but the one past the game's end. */
static void Timing_SetUpTheWorstCase(void)
{
    GameActor_Init(&s_game, TIMING_GAME_ID);
    Scoreboard_Init(&s_scoreboards[0]);
    Scoreboard_Init(&s_scoreboards[1]);
    Outbox_Init(&s_game_outbox, s_game_sent, GAME_OUTBOX_CAPACITY);
    Outbox_Init(&s_scoreboard_outbox, s_scoreboard_sent, SCOREBOARD_MOST_SENT);
    Message new_game = Timing_Message(MSG_NEW_GAME, TIMING_CLIENT_ID);
    new_game.payload.new_game.rules = k_ten_pin;
    Timing_Tell(new_game);
    for (uint8_t ball = 0U; ball < 11U; ball++) {
        Message roll = Timing_Message(MSG_ROLL, TIMING_CLIENT_ID);
        roll.payload.roll.pins = (ball < 10U) ? 10U : 7U;
        Timing_Tell(roll);
    }
    Timing_Tell(Timing_Message(MSG_SUBSCRIBE, TIMING_FIRST_SCOREBOARD_ID));
    Timing_Tell(Timing_Message(MSG_SUBSCRIBE, TIMING_SECOND_SCOREBOARD_ID));
    for (uint8_t ball = 0U; ball < 13U; ball++) {
        Message roll = Timing_Message(MSG_PINSETTER_ROLL, ACTOR_ID_NONE);
        roll.payload.roll.pins = 10U;
        Timing_Tell(roll);
    }
    s_game_before = s_game;
    s_scoreboards_before[0] = s_scoreboards[0];
    s_scoreboards_before[1] = s_scoreboards[1];
}

/* What the game sent, to the scoreboard each event is for: the observers' share of the work. */
static void Timing_TellTheScoreboards(void)
{
    for (uint8_t sent = 0U; sent < s_game_outbox.count; sent++) {
        const Message *event = &s_game_sent[sent];
        if (event->envelope.to == TIMING_FIRST_SCOREBOARD_ID) {
            s_scoreboard_outbox.count = 0U;
            Scoreboard_Handle(&s_scoreboards[0], event, &s_scoreboard_outbox);
        } else if (event->envelope.to == TIMING_SECOND_SCOREBOARD_ID) {
            s_scoreboard_outbox.count = 0U;
            Scoreboard_Handle(&s_scoreboards[1], event, &s_scoreboard_outbox);
        }
    }
}

static void Timing_Report(const char *what, uint32_t ticks, uint32_t runs)
{
    const uint32_t instructions = (ticks * TIMING_INSTRUCTIONS_PER_TICK) / runs;
    Semihosting_Write("TIMING: ");
    Semihosting_Write(what);
    Semihosting_Write(": ");
    Semihosting_WriteNumber(instructions);
    Semihosting_Write(" instructions, an estimated ");
    Semihosting_WriteNumber((instructions * TIMING_CPI_TENTHS_LOW) / (TIMING_CPU_MHZ * 10U));
    Semihosting_Write(" to ");
    Semihosting_WriteNumber((instructions * TIMING_CPI_TENTHS_HIGH) / (TIMING_CPU_MHZ * 10U));
    Semihosting_Write(" us at 120 MHz, CPI 1.0 to 2.0\n");
}

static void Timing_Calibrate(void)
{
    const uint32_t start = Timing_Now();
    __asm__ volatile(".rept 1000\n\tnop\n\t.endr");
    const uint32_t ticks = start - Timing_Now();
    Semihosting_Write("TIMING: calibration: 1000 NOPs took ");
    Semihosting_WriteNumber(ticks);
    Semihosting_Write(" ticks, 25 expected at one instruction a nanosecond\n");
}

int main(void)
{
    TIMING_TIMER_RELOAD = 0xFFFFFFFFU;
    TIMING_TIMER_VALUE = 0xFFFFFFFFU;
    TIMING_TIMER_CTRL = 1U;
    Timing_Calibrate();
    Timing_SetUpTheWorstCase();

    Message edit = Timing_Message(MSG_EDIT, TIMING_CLIENT_ID);
    edit.payload.edit.first_roll = 1U;
    edit.payload.edit.rolls_removed = 11U;
    uint32_t game_ticks = 0U;
    uint32_t scoreboard_ticks = 0U;
    uint8_t sent = 0U;
    for (uint32_t run = 0U; run < TIMING_EDIT_RUNS; run++) {
        s_game = s_game_before;
        s_scoreboards[0] = s_scoreboards_before[0];
        s_scoreboards[1] = s_scoreboards_before[1];
        const uint32_t start = Timing_Now();
        Timing_Tell(edit);
        const uint32_t handled = Timing_Now();
        Timing_TellTheScoreboards();
        game_ticks += start - handled;
        scoreboard_ticks += handled - Timing_Now();
        sent = s_game_outbox.count;
    }
    Semihosting_Write("TIMING: the worst-case edit sent ");
    Semihosting_WriteNumber(sent);
    Semihosting_Write(" messages, of an outbox of ");
    Semihosting_WriteNumber(GAME_OUTBOX_CAPACITY);
    Semihosting_Write("\n");
    Timing_Report("the worst-case edit, the game's handling", game_ticks, TIMING_EDIT_RUNS);
    Timing_Report("its events, both scoreboards' handling", scoreboard_ticks, TIMING_EDIT_RUNS);

    const Message query = Timing_Message(MSG_QUERY_FIGURE, TIMING_CLIENT_ID);
    uint32_t query_ticks = 0U;
    for (uint32_t run = 0U; run < TIMING_QUERY_RUNS; run++) {
        const uint32_t start = Timing_Now();
        Timing_Tell(query);
        query_ticks += start - Timing_Now();
    }
    Timing_Report("a QUERY_FIGURE, the game's handling", query_ticks, TIMING_QUERY_RUNS);
    Semihosting_Exit(sent == GAME_OUTBOX_CAPACITY);
}
