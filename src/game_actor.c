#include "game_actor.h"
#include "game_actor_state.h"

#include <assert.h>
#include <stddef.h>

#include "outbox.h"

void GameActor_Init(GameActor *self, ActorId id)
{
    self->id = id;
    self->lifecycle = GAME_AWAITING_RULES;
    Subscribers_Init(&self->subscribers);
    HeldRolls_Init(&self->held);
    self->lost_to_full_queue = 0U;
    self->not_understood = 0U;
}

static void GameOutbox_FrameChanged(Outbox *outbox, ActorId from, ActorId to,
                                    const FrameEvent *frame)
{
    Outbox_Next(outbox, Envelope_Event(MSG_FRAME_CHANGED, from, to))->payload.frame = *frame;
}

/* An event from the game, for its subscribers to be told. */
static Message GameActor_Event(const GameActor *self, Selector selector)
{
    Message event;
    event.envelope = Envelope_Event(selector, self->id, ACTOR_ID_NONE);
    return event;
}

static void GameActor_Publish(const GameActor *self, const FrameEvents *events,
                              Outbox *outbox)
{
    for (uint8_t e = 0U; e < events->count; e++) {
        Message event = GameActor_Event(self, MSG_FRAME_CHANGED);
        event.payload.frame = events->events[e];
        Subscribers_Tell(&self->subscribers, outbox, &event);
    }
}

/* The ball the oldest held roll would be: the next the scorer takes. */
static RollNumber GameActor_FirstHeldBallNumber(const GameActor *self)
{
    return (RollNumber)(Scorer_BallCount(&self->scorer) + 1U);
}

static void GameActor_PublishHeld(const GameActor *self, uint8_t index, Outbox *outbox)
{
    Message event = GameActor_Event(self, MSG_ROLL_HELD);
    event.payload.roll_held =
        HeldRolls_Report(&self->held, index, GameActor_FirstHeldBallNumber(self));
    Subscribers_Tell(&self->subscribers, outbox, &event);
}

static void GameActor_PublishLost(const GameActor *self, Outbox *outbox)
{
    Message event = GameActor_Event(self, MSG_ROLLS_LOST);
    event.payload.rolls_lost.lost =
        (uint16_t)(self->lost_to_full_queue + HeldRolls_Lost(&self->held));
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

static void GameActor_PlayOrHold(GameActor *self, Pins pins, Outbox *outbox)
{
    const GameStatus status = GameActor_Play(self, pins, outbox);
    if (status != GAME_OK) {
        HeldRolls_RefuseFirst(&self->held, status);
        GameActor_HoldOrLose(self, pins, outbox);
    }
}

static void GameActor_PinsetterRoll(GameActor *self, const Message *message,
                                    Outbox *outbox)
{
    if (HeldRolls_IsEmpty(&self->held)) {
        GameActor_PlayOrHold(self, message->payload.roll.pins, outbox);
    } else {
        GameActor_HoldOrLose(self, message->payload.roll.pins, outbox);
    }
}

static RollEdit Message_Edit(const Message *message)
{
    const EditPayload *payload = &message->payload.edit;
    const RollEdit edit = { payload->first_roll, payload->rolls_removed,
                            (payload->new_count > 0U) ? payload->new_pins : NULL,
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
    if (HeldRolls_IsEmpty(&self->held)) {
        Outbox_Reply(outbox, message, GAME_ERR_NO_SUCH_ROLL, 0U);
        return;
    }
    Message *reply = Outbox_BeginReply(outbox, message);
    HeldRolls_DropFirst(&self->held);
    GameActor_LetHeldRollsThrough(self, outbox);
    Outbox_FinishReply(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_RollsLost(GameActor *self, const Message *message, Outbox *outbox)
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
    stats->rolls_lost = (uint16_t)(self->lost_to_full_queue + HeldRolls_Lost(&self->held));
    stats->rolls_held = HeldRolls_Count(&self->held);
    if (self->lifecycle == GAME_IN_PLAY) { /* before a game, the scorer holds no game at all */
        FrameEvents complete;
        Scorer_ReportCompleteFrames(&self->scorer, &complete);
        stats->complete_frames = complete.count;
        stats->total = Scorer_Score(&self->scorer);
    }
}

/* The game's figure: its total. */
static void GameActor_AnswerFigure(const GameActor *self, const Message *message, Outbox *outbox)
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
    Outbox_NotUnderstood(outbox, self->id, message);
}

/* What the game makes of each selector of the protocol. The ones it doesn't answer are the ones
 * not listed, which read as GAME_DOES_NOT_UNDERSTAND. */
typedef enum {
    GAME_DOES_NOT_UNDERSTAND = 0,
    GAME_ROLL,
    GAME_SUBSCRIBE,
    GAME_UNSUBSCRIBE,
    GAME_EDIT,
    GAME_PINSETTER_ROLL,
    GAME_QUERY_FIGURE,
    GAME_DISCARD_HELD,
    GAME_ROLLS_LOST
} GameRequest;

static const GameRequest k_game_requests[MSG_SELECTOR_COUNT] = {
    [MSG_ROLL] = GAME_ROLL,
    [MSG_SUBSCRIBE] = GAME_SUBSCRIBE,
    [MSG_UNSUBSCRIBE] = GAME_UNSUBSCRIBE,
    [MSG_EDIT] = GAME_EDIT,
    [MSG_PINSETTER_ROLL] = GAME_PINSETTER_ROLL,
    [MSG_QUERY_FIGURE] = GAME_QUERY_FIGURE,
    [MSG_DISCARD_HELD] = GAME_DISCARD_HELD,
    [MSG_ROLLS_LOST] = GAME_ROLLS_LOST,
};

static GameRequest GameActor_RequestOf(const Message *message)
{
    const Selector selector = message->envelope.selector;
    if (!Selector_IsInProtocol(selector)) {
        return GAME_DOES_NOT_UNDERSTAND;
    }
    return k_game_requests[selector];
}

static bool GameActor_IsPlayingAGame(const GameActor *self)
{
    return (self->lifecycle == GAME_IN_PLAY) && !Scorer_IsOver(&self->scorer);
}

/* The old game's complete frames, each as no longer complete: what the new game's start does to
 * a subscriber's view. None before the first game. */
static void GameActor_ReopenEveryFrame(const GameActor *self, FrameEvents *reopened)
{
    if (self->lifecycle != GAME_IN_PLAY) {
        reopened->count = 0U;
        return;
    }
    Scorer_ReportReopened(&self->scorer, reopened);
}

static void GameActor_NewGame(GameActor *self, const Message *message, Outbox *outbox)
{
    if (GameActor_IsPlayingAGame(self)) {
        Outbox_Reply(outbox, message, GAME_ERR_GAME_IN_PROGRESS, 0U);
        return;
    }
    Message *reply = Outbox_BeginReply(outbox, message);
    FrameEvents reopened;
    GameActor_ReopenEveryFrame(self, &reopened);
    const GameStatus status = Scorer_Start(&self->scorer, &message->payload.new_game.rules);
    if (status != GAME_OK) {
        Outbox_FinishReply(reply, status, 0U);
        return;
    }
    self->lifecycle = GAME_IN_PLAY;
    GameActor_Publish(self, &reopened, outbox);
    GameActor_LetHeldRollsThrough(self, outbox);
    Outbox_FinishReply(reply, GAME_OK, Scorer_Score(&self->scorer));
}

static void GameActor_Receive(GameActor *self, const Message *message, Outbox *outbox)
{
    switch (GameActor_RequestOf(message)) {
    case GAME_SUBSCRIBE:
        GameActor_Subscribe(self, message, outbox);
        break;
    case GAME_UNSUBSCRIBE:
        GameActor_Unsubscribe(self, message, outbox);
        break;
    case GAME_EDIT:
        GameActor_Edit(self, message, outbox);
        break;
    case GAME_PINSETTER_ROLL:
        GameActor_PinsetterRoll(self, message, outbox);
        break;
    case GAME_DISCARD_HELD:
        GameActor_DiscardHeld(self, message, outbox);
        break;
    case GAME_ROLLS_LOST:
        GameActor_RollsLost(self, message, outbox);
        break;
    case GAME_QUERY_FIGURE:
        GameActor_AnswerFigure(self, message, outbox);
        break;
    case GAME_ROLL:
        GameActor_Roll(self, message, outbox);
        break;
    case GAME_DOES_NOT_UNDERSTAND:
        GameActor_DoesNotUnderstand(self, message, outbox);
        break;
    }
}

/* Before any game: the pinsetter's rolls wait for the first, and its losses are counted. Any
 * other request is answered "no game". */
static void GameActor_BeforeAGame(GameActor *self, const Message *message, Outbox *outbox)
{
    const GameRequest request = GameActor_RequestOf(message);
    if (request == GAME_PINSETTER_ROLL) {
        GameActor_HoldOrLose(self, message->payload.roll.pins, outbox);
    } else if (request == GAME_ROLLS_LOST) {
        GameActor_RollsLost(self, message, outbox);
    } else {
        Outbox_Reply(outbox, message, GAME_ERR_NO_GAME, 0U);
    }
}

/* What every state treats alike first: the lifecycle's own message, and the statistics, which
 * every kind answers whatever its state. Then what the lifecycle makes of the rest. */
void GameActor_Handle(GameActor *self, const Message *message, Outbox *outbox)
{
    if (message->envelope.selector == MSG_NEW_GAME) {
        GameActor_NewGame(self, message, outbox);
        return;
    }
    if (message->envelope.selector == MSG_QUERY_STATS) {
        GameActor_AnswerStats(self, message, outbox);
        return;
    }
    switch (self->lifecycle) {
    case GAME_AWAITING_RULES:
        GameActor_BeforeAGame(self, message, outbox);
        break;
    case GAME_IN_PLAY:
        GameActor_Receive(self, message, outbox);
        break;
    }
}
