#include "game_actor.h"
#include "game_actor_state.h"

#include <assert.h>

#include "outbox.h"

void GameActor_Init(GameActor *self, ActorId id)
{
    self->id = id;
    self->lifecycle = GAME_AWAITING_RULES;
    Subscribers_Init(&self->subscribers);
    HeldRolls_Init(&self->held);
    self->lost_to_full_queue = 0U;
    self->not_understood = 0U;
    self->practice_balls = 0U;
}

static void GameOutbox_FrameChanged(Outbox *outbox, ActorId from, ActorId to,
                                    const FrameEvent *frame)
{
    Outbox_Next(outbox, Envelope_Event(MSG_FRAME_CHANGED, from, to))->payload.frame = *frame;
}

static bool GameActor_HasHadAGame(const GameActor *self)
{
    return self->lifecycle != GAME_AWAITING_RULES;
}

static Message GameActor_EventForItsSubscribers(const GameActor *self, Selector selector)
{
    Message event;
    event.envelope = Envelope_Event(selector, self->id, ACTOR_ID_NONE);
    return event;
}

static void GameActor_Publish(const GameActor *self, const FrameEvents *events,
                              Outbox *outbox)
{
    for (uint8_t e = 0U; e < events->count; e++) {
        Message event = GameActor_EventForItsSubscribers(self, MSG_FRAME_CHANGED);
        event.payload.frame = events->events[e];
        Subscribers_Tell(&self->subscribers, outbox, &event);
    }
}

static RollNumber GameActor_NextBallNumber(const GameActor *self)
{
    return (RollNumber)(Scorer_BallCount(&self->scorer) + 1U);
}

static void GameActor_PublishHeld(const GameActor *self, uint8_t index, Outbox *outbox)
{
    Message event = GameActor_EventForItsSubscribers(self, MSG_ROLL_HELD);
    event.payload.roll_held =
        HeldRolls_Report(&self->held, index, GameActor_NextBallNumber(self));
    Subscribers_Tell(&self->subscribers, outbox, &event);
}

static uint16_t GameActor_RollsLostEverywhere(const GameActor *self)
{
    return (uint16_t)(self->lost_to_full_queue + HeldRolls_Lost(&self->held));
}

static void GameActor_PublishLost(const GameActor *self, Outbox *outbox)
{
    Message event = GameActor_EventForItsSubscribers(self, MSG_ROLLS_LOST);
    event.payload.rolls_lost.lost = GameActor_RollsLostEverywhere(self);
    Subscribers_Tell(&self->subscribers, outbox, &event);
}

static void GameActor_HoldOrLose(GameActor *self, Pins pins, Outbox *outbox)
{
    if (HeldRolls_Hold(&self->held, pins)) {
        GameActor_PublishHeld(self, HeldRolls_NewestIndex(&self->held), outbox);
    } else {
        GameActor_PublishLost(self, outbox);
    }
}

static GameStatus GameActor_Play(GameActor *self, Pins pins, Outbox *outbox)
{
    FrameEvents events;
    const GameStatus status = Scorer_Roll(&self->scorer, pins, &events);
    GameActor_Publish(self, &events, outbox);
    return status;
}

static void GameActor_LetHeldRollsThrough(GameActor *self, Outbox *outbox)
{
    while (!HeldRolls_IsEmpty(&self->held)) {
        const GameStatus status = GameActor_Play(self, HeldRolls_Oldest(&self->held), outbox);
        if (status != GAME_OK) {
            HeldRolls_RefuseFirst(&self->held, status);
            GameActor_PublishHeld(self, 0U, outbox);
            return;
        }
        HeldRolls_DropFirst(&self->held);
    }
}

static void GameActor_Roll(GameActor *self, const Message *message, Outbox *outbox)
{
    Message *reply = Outbox_BeginReply(outbox, message);
    const GameStatus status = GameActor_Play(self, message->payload.roll.pins, outbox);
    Outbox_FinishReply(reply, status, Scorer_Score(&self->scorer));
}

/* In play with nothing held: the first roll the scorer refuses is held, and the game is holding. */
static void GameActor_PlayOrHold(GameActor *self, const Message *message, Outbox *outbox)
{
    const Pins pins = message->payload.roll.pins;
    const GameStatus status = GameActor_Play(self, pins, outbox);
    if (status != GAME_OK) {
        HeldRolls_RefuseFirst(&self->held, status);
        GameActor_HoldOrLose(self, pins, outbox);
    }
}

static RollEdit Message_Edit(const Message *message)
{
    const EditPayload *payload = &message->payload.edit;
    const RollEdit edit = { payload->first_roll, payload->rolls_removed, payload->new_pins,
                            payload->new_count };
    return edit;
}

static void GameActor_Edit(GameActor *self, const Message *message, Outbox *outbox)
{
    Message *reply = Outbox_BeginReply(outbox, message);
    const RollEdit edit = Message_Edit(message);
    FrameEvents events;
    const GameStatus status = Scorer_Edit(&self->scorer, &edit, &events);
    GameActor_Publish(self, &events, outbox);
    if (status == GAME_OK) {
        GameActor_LetHeldRollsThrough(self, outbox);
    }
    Outbox_FinishReply(reply, status, Scorer_Score(&self->scorer));
}

static void GameActor_DiscardHeld(GameActor *self, const Message *message,
                                  Outbox *outbox)
{
    Message *reply = Outbox_BeginReply(outbox, message);
    HeldRolls_DropFirst(&self->held);
    GameActor_LetHeldRollsThrough(self, outbox);
    Outbox_FinishReply(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_HearLostReport(GameActor *self, const Message *message, Outbox *outbox)
{
    if (message->payload.rolls_lost.lost != self->lost_to_full_queue) {
        self->lost_to_full_queue = message->payload.rolls_lost.lost;
        GameActor_PublishLost(self, outbox);
    }
}

static void GameActor_AnswerStats(const GameActor *self, const Message *message, Outbox *outbox)
{
    StatsPayload *stats = Outbox_BeginStats(outbox, message);
    stats->not_understood = self->not_understood;
    stats->rolls_lost = GameActor_RollsLostEverywhere(self);
    stats->rolls_held = HeldRolls_Count(&self->held);
    stats->practice_balls = self->practice_balls;
    if (GameActor_HasHadAGame(self)) {
        FrameEvents complete;
        Scorer_ReportCompleteFrames(&self->scorer, &complete);
        stats->complete_frames = complete.count;
        stats->total = Scorer_Score(&self->scorer);
    }
}

static void GameActor_AnswerItsTotalAsItsFigure(const GameActor *self, const Message *message,
                                                Outbox *outbox)
{
    Outbox_Reply(outbox, message, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_SendCompleteFrames(const GameActor *self, ActorId subscriber,
                                        Outbox *outbox)
{
    FrameEvents complete;
    Scorer_ReportCompleteFrames(&self->scorer, &complete);
    for (uint8_t e = 0U; e < complete.count; e++) {
        GameOutbox_FrameChanged(outbox, self->id, subscriber, &complete.events[e]);
    }
}

static void GameActor_Subscribe(GameActor *self, const Message *message, Outbox *outbox)
{
    if (Subscribers_IsFull(&self->subscribers)) {
        Outbox_Reply(outbox, message, GAME_ERR_TOO_MANY_SUBSCRIBERS, 0U);
        return;
    }
    Subscribers_Add(&self->subscribers, message->envelope.from);
    Outbox_Reply(outbox, message, GAME_OK, 0U);
    GameActor_SendCompleteFrames(self, message->envelope.from, outbox);
}

static void GameActor_Unsubscribe(GameActor *self, const Message *message,
                                  Outbox *outbox)
{
    const bool removed = Subscribers_Remove(&self->subscribers, message->envelope.from);
    Outbox_Reply(outbox, message, removed ? GAME_OK : GAME_ERR_NOT_SUBSCRIBED, 0U);
}

static void GameActor_DoesNotUnderstand(GameActor *self, const Message *message,
                                        Outbox *outbox)
{
    self->not_understood++;
    Outbox_NotUnderstood(outbox, message);
}

static bool GameActor_IsPlayingAGame(const GameActor *self)
{
    return GameActor_HasHadAGame(self) && !Scorer_IsOver(&self->scorer);
}

static void GameActor_ReopenTheOldGamesFrames(const GameActor *self, FrameEvents *reopened)
{
    if (!GameActor_HasHadAGame(self)) {
        reopened->count = 0U;
        return;
    }
    Scorer_ReportReopened(&self->scorer, reopened);
}

/* The rolls held since the last game were someone rolling on a dead lane: a practice doesn't
 * play them. */
static void GameActor_LoseTheRollsOnADeadLane(GameActor *self, Outbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        return;
    }
    HeldRolls_LoseAll(&self->held);
    GameActor_PublishLost(self, outbox);
}

/* Refused, changing nothing, if the scorer can't play the rules. */
static GameStatus GameActor_StartNextGame(GameActor *self, const NewGamePayload *new_game,
                                          Outbox *outbox)
{
    FrameEvents reopened;
    GameActor_ReopenTheOldGamesFrames(self, &reopened);
    const GameStatus status = Scorer_Start(&self->scorer, &new_game->rules);
    if (status != GAME_OK) {
        return status;
    }
    self->lifecycle = new_game->practice ? GAME_PRACTICE : GAME_IN_PLAY;
    self->practice_balls = 0U;
    GameActor_Publish(self, &reopened, outbox);
    if (new_game->practice) {
        GameActor_LoseTheRollsOnADeadLane(self, outbox);
    } else {
        GameActor_LetHeldRollsThrough(self, outbox);
    }
    return GAME_OK;
}

static void GameActor_NewGame(GameActor *self, const Message *message, Outbox *outbox)
{
    if (GameActor_IsPlayingAGame(self)) {
        Outbox_Reply(outbox, message, GAME_ERR_GAME_IN_PROGRESS, 0U);
        return;
    }
    Message *reply = Outbox_BeginReply(outbox, message);
    const GameStatus status =
        GameActor_StartNextGame(self, &message->payload.new_game, outbox);
    Outbox_FinishReply(reply, status, (status == GAME_OK) ? Scorer_Score(&self->scorer) : 0U);
}

/* A request this state refuses, for its reason: "no game" before a game, "no such roll" to a
 * discard with nothing held. */
static void GameActor_Refuse(const Message *message, GameStatus why, Outbox *outbox)
{
    Outbox_Reply(outbox, message, why, 0U);
}

/* A ball in practice: counted, not scored. */
static void GameActor_CountPracticeBall(GameActor *self, const Message *message, Outbox *outbox)
{
    self->practice_balls++;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
}

static void GameActor_HoldTheRoll(GameActor *self, const Message *message, Outbox *outbox)
{
    GameActor_HoldOrLose(self, message->payload.roll.pins, outbox);
}

/* The state a message is read in: the lifecycle's decision, and what the held list says. */
typedef enum {
    GAME_STATE_AWAITING_RULES,
    GAME_STATE_PRACTICE,
    GAME_STATE_IN_PLAY,
    GAME_STATE_HOLDING, /* in play, with rolls held */
    GAME_STATES
} GameState;

static GameState GameActor_State(const GameActor *self)
{
    if (self->lifecycle == GAME_AWAITING_RULES) {
        return GAME_STATE_AWAITING_RULES;
    }
    if (self->lifecycle == GAME_PRACTICE) {
        return GAME_STATE_PRACTICE;
    }
    return HeldRolls_IsEmpty(&self->held) ? GAME_STATE_IN_PLAY : GAME_STATE_HOLDING;
}

/* What a message means to the game in each state: an answer, which leaves the game as it is; a
 * play, which changes the game's balls; or a move, which changes its lifecycle. Every entry names
 * all three, all but one of them nothing. Three switches, not one: together they are more cases
 * than ENG-3.1 allows one function. */
typedef enum {
    GAME_DOES_NOT_UNDERSTAND = 0, /* what a missing entry reads as */
    GAME_NO_ANSWER,
    GAME_REFUSE, /* with the entry's reason */
    GAME_ANSWER_FIGURE,
    GAME_ANSWER_STATS,
    GAME_SUBSCRIBE,
    GAME_UNSUBSCRIBE
} GameAnswer;

typedef enum {
    GAME_NO_PLAY = 0,
    GAME_ROLL,
    GAME_PLAY_OR_HOLD,
    GAME_HOLD_THE_ROLL,
    GAME_EDIT,
    GAME_DISCARD_HELD,
    GAME_HEAR_LOST_REPORT,
    GAME_COUNT_PRACTICE_BALL
} GamePlay;

typedef enum {
    GAME_NO_MOVE = 0,
    GAME_NEW_GAME,
    GAME_END_PRACTICE
} GameMove;

typedef struct {
    GameAnswer answer;
    GamePlay play;
    GameMove move;
    GameStatus refused_for; /* GAME_REFUSE's reason; GAME_OK otherwise */
} GameMeaning;

/* One row per selector, and one, at MSG_SELECTOR_COUNT, for a selector outside the protocol. */
#define GAME_PROTOCOL_ROWS (MSG_SELECTOR_COUNT + 1U)

static const GameMeaning k_game_protocols[GAME_STATES][GAME_PROTOCOL_ROWS] = {
    [GAME_STATE_AWAITING_RULES] = {
        [MSG_NEW_GAME] = { GAME_NO_ANSWER, GAME_NO_PLAY, GAME_NEW_GAME, GAME_OK },
        [MSG_ROLL] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_SUBSCRIBE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_UNSUBSCRIBE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_EDIT] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_PINSETTER_ROLL] = { GAME_NO_ANSWER, GAME_HOLD_THE_ROLL, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_FIGURE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_DISCARD_HELD] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_ROLLS_LOST] = { GAME_NO_ANSWER, GAME_HEAR_LOST_REPORT, GAME_NO_MOVE, GAME_OK },
        [MSG_REPLY] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_FRAME_CHANGED] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_ROLL_HELD] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_NOT_UNDERSTOOD] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_QUERY_STATS] = { GAME_ANSWER_STATS, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_STATS] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_END_PRACTICE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_PINSETTER_DOWN] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_PINSETTER_UP] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
        [MSG_SELECTOR_COUNT] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_GAME },
    },
    [GAME_STATE_PRACTICE] = {
        [MSG_NEW_GAME] = { GAME_NO_ANSWER, GAME_NO_PLAY, GAME_NEW_GAME, GAME_OK },
        [MSG_ROLL] = { GAME_NO_ANSWER, GAME_COUNT_PRACTICE_BALL, GAME_NO_MOVE, GAME_OK },
        [MSG_SUBSCRIBE] = { GAME_SUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_UNSUBSCRIBE] = { GAME_UNSUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_EDIT] = { GAME_NO_ANSWER, GAME_EDIT, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_ROLL] = { GAME_NO_ANSWER, GAME_COUNT_PRACTICE_BALL, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_FIGURE] = { GAME_ANSWER_FIGURE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_DISCARD_HELD] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_SUCH_ROLL },
        [MSG_ROLLS_LOST] = { GAME_NO_ANSWER, GAME_HEAR_LOST_REPORT, GAME_NO_MOVE, GAME_OK },
        [MSG_REPLY] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_FRAME_CHANGED] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_ROLL_HELD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_NOT_UNDERSTOOD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_STATS] = { GAME_ANSWER_STATS, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_STATS] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_END_PRACTICE] = { GAME_NO_ANSWER, GAME_NO_PLAY, GAME_END_PRACTICE, GAME_OK },
        [MSG_PINSETTER_DOWN] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_UP] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_SELECTOR_COUNT] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
    },
    [GAME_STATE_IN_PLAY] = {
        [MSG_NEW_GAME] = { GAME_NO_ANSWER, GAME_NO_PLAY, GAME_NEW_GAME, GAME_OK },
        [MSG_ROLL] = { GAME_NO_ANSWER, GAME_ROLL, GAME_NO_MOVE, GAME_OK },
        [MSG_SUBSCRIBE] = { GAME_SUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_UNSUBSCRIBE] = { GAME_UNSUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_EDIT] = { GAME_NO_ANSWER, GAME_EDIT, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_ROLL] = { GAME_NO_ANSWER, GAME_PLAY_OR_HOLD, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_FIGURE] = { GAME_ANSWER_FIGURE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_DISCARD_HELD] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NO_SUCH_ROLL },
        [MSG_ROLLS_LOST] = { GAME_NO_ANSWER, GAME_HEAR_LOST_REPORT, GAME_NO_MOVE, GAME_OK },
        [MSG_REPLY] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_FRAME_CHANGED] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_ROLL_HELD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_NOT_UNDERSTOOD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_STATS] = { GAME_ANSWER_STATS, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_STATS] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_END_PRACTICE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NOT_IN_PRACTICE },
        [MSG_PINSETTER_DOWN] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_UP] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_SELECTOR_COUNT] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
    },

    [GAME_STATE_HOLDING] = {
        [MSG_NEW_GAME] = { GAME_NO_ANSWER, GAME_NO_PLAY, GAME_NEW_GAME, GAME_OK },
        [MSG_ROLL] = { GAME_NO_ANSWER, GAME_ROLL, GAME_NO_MOVE, GAME_OK },
        [MSG_SUBSCRIBE] = { GAME_SUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_UNSUBSCRIBE] = { GAME_UNSUBSCRIBE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_EDIT] = { GAME_NO_ANSWER, GAME_EDIT, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_ROLL] = { GAME_NO_ANSWER, GAME_HOLD_THE_ROLL, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_FIGURE] = { GAME_ANSWER_FIGURE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_DISCARD_HELD] = { GAME_NO_ANSWER, GAME_DISCARD_HELD, GAME_NO_MOVE, GAME_OK },
        [MSG_ROLLS_LOST] = { GAME_NO_ANSWER, GAME_HEAR_LOST_REPORT, GAME_NO_MOVE, GAME_OK },
        [MSG_REPLY] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_FRAME_CHANGED] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_ROLL_HELD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_NOT_UNDERSTOOD] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_QUERY_STATS] = { GAME_ANSWER_STATS, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_STATS] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_END_PRACTICE] = { GAME_REFUSE, GAME_NO_PLAY, GAME_NO_MOVE, GAME_ERR_NOT_IN_PRACTICE },
        [MSG_PINSETTER_DOWN] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_PINSETTER_UP] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
        [MSG_SELECTOR_COUNT] = { GAME_DOES_NOT_UNDERSTAND, GAME_NO_PLAY, GAME_NO_MOVE, GAME_OK },
    },
};

static GameMeaning GameActor_MeaningOf(const GameActor *self, const Message *message)
{
    const Selector selector = message->envelope.selector;
    const unsigned row = Selector_IsInProtocol(selector) ? (unsigned)selector : MSG_SELECTOR_COUNT;
    return k_game_protocols[GameActor_State(self)][row];
}

static void GameActor_Answer(GameActor *self, GameMeaning meaning, const Message *message,
                             Outbox *outbox)
{
    switch (meaning.answer) {
    case GAME_DOES_NOT_UNDERSTAND:
        GameActor_DoesNotUnderstand(self, message, outbox);
        break;
    case GAME_NO_ANSWER:
        break;
    case GAME_REFUSE:
        GameActor_Refuse(message, meaning.refused_for, outbox);
        break;
    case GAME_ANSWER_FIGURE:
        GameActor_AnswerItsTotalAsItsFigure(self, message, outbox);
        break;
    case GAME_ANSWER_STATS:
        GameActor_AnswerStats(self, message, outbox);
        break;
    case GAME_SUBSCRIBE:
        GameActor_Subscribe(self, message, outbox);
        break;
    case GAME_UNSUBSCRIBE:
        GameActor_Unsubscribe(self, message, outbox);
        break;
    }
}

static void GameActor_MakeThePlay(GameActor *self, GamePlay play, const Message *message,
                                  Outbox *outbox)
{
    switch (play) {
    case GAME_NO_PLAY:
        break;
    case GAME_ROLL:
        GameActor_Roll(self, message, outbox);
        break;
    case GAME_PLAY_OR_HOLD:
        GameActor_PlayOrHold(self, message, outbox);
        break;
    case GAME_HOLD_THE_ROLL:
        GameActor_HoldTheRoll(self, message, outbox);
        break;
    case GAME_EDIT:
        GameActor_Edit(self, message, outbox);
        break;
    case GAME_DISCARD_HELD:
        GameActor_DiscardHeld(self, message, outbox);
        break;
    case GAME_HEAR_LOST_REPORT:
        GameActor_HearLostReport(self, message, outbox);
        break;
    case GAME_COUNT_PRACTICE_BALL:
        GameActor_CountPracticeBall(self, message, outbox);
        break;
    }
}

static void GameActor_EndPractice(GameActor *self, const Message *message, Outbox *outbox)
{
    self->lifecycle = GAME_IN_PLAY;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
}

static void GameActor_MakeTheMove(GameActor *self, GameMove move, const Message *message,
                                  Outbox *outbox)
{
    switch (move) {
    case GAME_NO_MOVE:
        break;
    case GAME_NEW_GAME:
        GameActor_NewGame(self, message, outbox);
        break;
    case GAME_END_PRACTICE:
        GameActor_EndPractice(self, message, outbox);
        break;
    }
}

void GameActor_Handle(GameActor *self, const Message *message, Outbox *outbox)
{
    const GameMeaning meaning = GameActor_MeaningOf(self, message);
    GameActor_Answer(self, meaning, message, outbox);
    GameActor_MakeThePlay(self, meaning.play, message, outbox);
    GameActor_MakeTheMove(self, meaning.move, message, outbox);
}
