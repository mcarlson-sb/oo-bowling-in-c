#include "game_actor.h"
#include "game_actor_state.h"

#include "game_protocol.h"
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
    self->pinsetter_down = false;
    self->rolls_refused = 0U;
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

/* Rolls held mid-game that a correction leaves past the game's end are rolls on a dead lane:
 * lost, and counted. */
static void GameActor_LoseTheRollsOnADeadLane(GameActor *self, Outbox *outbox)
{
    HeldRolls_LoseAll(&self->held);
    GameActor_PublishLost(self, outbox);
}

static void GameActor_LetHeldRollsThrough(GameActor *self, Outbox *outbox)
{
    while (!HeldRolls_IsEmpty(&self->held)) {
        const GameStatus status = GameActor_Play(self, HeldRolls_Oldest(&self->held), outbox);
        if (status == GAME_ERR_GAME_OVER) {
            GameActor_LoseTheRollsOnADeadLane(self, outbox);
            return;
        }
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
    Outbox_FinishReply(reply, ReplyStatus_OfGame(status), Scorer_Score(&self->scorer));
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
    Outbox_FinishReply(reply, ReplyStatus_OfGame(status), Scorer_Score(&self->scorer));
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
    stats->rolls_refused = self->rolls_refused;
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
    const Envelope envelope = Envelope_Event(MSG_FRAME_CHANGED, self->id, subscriber);
    for (uint8_t e = 0U; e < complete.count; e++) {
        Outbox_Next(outbox, envelope)->payload.frame = complete.events[e];
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

static void GameActor_ReopenTheOldGamesFrames(const GameActor *self, FrameEvents *reopened)
{
    if (!GameActor_HasHadAGame(self)) {
        reopened->count = 0U;
        return;
    }
    Scorer_ReportReopened(&self->scorer, reopened);
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
    return GAME_OK;
}

static void GameActor_NewGame(GameActor *self, const Message *message, Outbox *outbox)
{
    Message *reply = Outbox_BeginReply(outbox, message);
    const GameStatus status =
        GameActor_StartNextGame(self, &message->payload.new_game, outbox);
    Outbox_FinishReply(reply, ReplyStatus_OfGame(status),
                       (status == GAME_OK) ? Scorer_Score(&self->scorer) : 0U);
}

/* A request this state refuses, for its reason: "no game" before a game, "no such roll" to a
 * discard with nothing held. */
static void GameActor_Refuse(const Message *message, GameStatus why, Outbox *outbox)
{
    Outbox_Reply(outbox, message, ReplyStatus_OfGame(why), 0U);
}

/* A ball in practice: counted, not scored. */
static void GameActor_CountPracticeBall(GameActor *self, const Message *message, Outbox *outbox)
{
    self->practice_balls++;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
}

/* The pinsetter's roll refused, down or once the game is over or certified: counted, and from no
 * one, so not answered. */
static void GameActor_RefuseThePinsettersRoll(GameActor *self)
{
    self->rolls_refused++;
}

static void GameActor_HoldTheRoll(GameActor *self, const Message *message, Outbox *outbox)
{
    GameActor_HoldOrLose(self, message->payload.roll.pins, outbox);
}

static GameState GameActor_State(const GameActor *self)
{
    if (self->lifecycle == GAME_AWAITING_RULES) {
        return GAME_STATE_AWAITING_RULES;
    }
    if (self->lifecycle == GAME_PRACTICE) {
        return GAME_STATE_PRACTICE;
    }
    if (self->lifecycle == GAME_CERTIFIED) {
        return GAME_STATE_CERTIFIED;
    }
    if (!HeldRolls_IsEmpty(&self->held)) {
        return GAME_STATE_HOLDING;
    }
    return Scorer_IsOver(&self->scorer) ? GAME_STATE_OVER : GAME_STATE_IN_PLAY;
}

static void GameActor_DoWhatItAsks(GameActor *self, GameMeaning meaning, const Message *message,
                                   Outbox *outbox)
{
    switch (meaning.request) {
    case GAME_DOES_NOT_UNDERSTAND:
        GameActor_DoesNotUnderstand(self, message, outbox);
        break;
    case GAME_NOTHING_ASKED:
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
    case GAME_REFUSE_THE_PINSETTERS_ROLL:
        GameActor_RefuseThePinsettersRoll(self);
        break;
    }
}

static void GameActor_EndPractice(GameActor *self, const Message *message, Outbox *outbox)
{
    self->lifecycle = GAME_IN_PLAY;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
}

/* Down or up, whatever the lifecycle: only the pinsetter's rolls heed it. */
static void GameActor_SetThePinsetter(GameActor *self, bool down, const Message *message,
                                      Outbox *outbox)
{
    self->pinsetter_down = down;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
}

/* Only once the game is over, which its state says. */
static void GameActor_Certify(GameActor *self, const Message *message, Outbox *outbox)
{
    self->lifecycle = GAME_CERTIFIED;
    Outbox_Reply(outbox, message, REPLY_OK, 0U);
    const Message certified = GameActor_EventForItsSubscribers(self, MSG_CERTIFIED);
    Subscribers_Tell(&self->subscribers, outbox, &certified);
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
    case GAME_PINSETTER_DOWN:
        GameActor_SetThePinsetter(self, true, message, outbox);
        break;
    case GAME_PINSETTER_UP:
        GameActor_SetThePinsetter(self, false, message, outbox);
        break;
    case GAME_CERTIFY:
        GameActor_Certify(self, message, outbox);
        break;
    }
}

void GameActor_Handle(GameActor *self, const Message *message, Outbox *outbox)
{
    const GameMeaning meaning = GameProtocol_MeaningOf(GameActor_State(self), self->pinsetter_down,
                                                       message->envelope.selector);
    GameActor_DoWhatItAsks(self, meaning, message, outbox);
    GameActor_MakeTheMove(self, meaning.move, message, outbox);
}
