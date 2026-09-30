/* What the protocol promises of every kind, whatever sits at an id: that a client can always ask
 * for the facts, with QUERY_STATS, without knowing which kind it asks; and that no kind, in any
 * state, answers an answer. */

#include <gtest/gtest.h>

#include "test_outbox.h"

#include <algorithm>
#include <deque>
#include <vector>

#include "rules_presets.h"

extern "C" {
#include "game_actor.h"
#include "game_actor_state.h"
#include "running_average.h"
#include "running_average_state.h"
#include "scoreboard.h"
#include "scoreboard_state.h"
}

namespace {

constexpr ActorId kKind = 2U;
constexpr ActorId kAsker = 4U;

Message StatsQuery(RequestSeq seq)
{
    Message message = {};
    message.envelope.selector = MSG_QUERY_STATS;
    message.envelope.from = kAsker;
    message.envelope.to = kKind;
    message.envelope.seq = seq;
    return message;
}

void ExpectStatsIn(const TestOutbox &outbox, RequestSeq seq, const char *kind)
{
    ASSERT_EQ(1U, outbox.count) << kind;
    EXPECT_EQ(MSG_STATS, outbox.items[0].envelope.selector) << kind;
    EXPECT_EQ(kAsker, outbox.items[0].envelope.to) << kind;
    EXPECT_EQ(seq, outbox.items[0].envelope.seq) << kind;
}

} // namespace

TEST(ProtocolTest, should_have_every_kind_answer_its_statistics_as_it_starts_even_a_game_before_its_first)
{
    /* An outbox each, as each kind's hosting task has its own. */
    TestOutbox to_the_game;
    GameActor game;
    GameActor_Init(&game, kKind);
    const Message to_game = StatsQuery(1U);
    GameActor_Handle(&game, &to_game, &to_the_game);
    ExpectStatsIn(to_the_game, 1U, "a game");
    TestOutbox to_the_board;
    Scoreboard board;
    Scoreboard_Init(&board);
    const Message to_board = StatsQuery(2U);
    Scoreboard_Handle(&board, &to_board, &to_the_board);
    ExpectStatsIn(to_the_board, 2U, "a scoreboard");
    TestOutbox to_the_average;
    RunningAverage average;
    RunningAverage_Init(&average);
    const Message to_average = StatsQuery(3U);
    RunningAverage_Handle(&average, &to_average, &to_the_average);
    ExpectStatsIn(to_the_average, 3U, "a running average");
}

namespace {

constexpr ActorId kAwaitingRules = 2U;
constexpr ActorId kInPlay = 3U;

/* Delivers every message in flight between two games, and what each makes the other send, up to
 * `most` deliveries. What is still in flight after that is returned. */
std::deque<Message> Exchange(GameActor *awaiting, GameActor *in_play, std::deque<Message> in_flight,
                             int most)
{
    for (int delivered = 0; !in_flight.empty() && (delivered < most); delivered++) {
        const Message message = in_flight.front();
        in_flight.pop_front();
        GameActor *to = (message.envelope.to == kAwaitingRules) ? awaiting : in_play;
        TestOutbox outbox;
        GameActor_Handle(to, &message, &outbox);
        in_flight.insert(in_flight.end(), outbox.items, outbox.items + outbox.count);
    }
    return in_flight;
}

} // namespace

TEST(ProtocolTest, should_end_an_exchange_between_a_game_awaiting_rules_and_a_game_in_play)
{
    /* A reply that reaches the wrong game: answered, it would start an exchange of answers. */
    GameActor awaiting;
    GameActor_Init(&awaiting, kAwaitingRules);
    GameActor in_play;
    GameActor_Init(&in_play, kInPlay);
    Message new_game = {};
    new_game.envelope = Envelope_Event(MSG_NEW_GAME, kAsker, kInPlay);
    new_game.payload.new_game.rules = rules::kTenPin;
    TestOutbox started;
    GameActor_Handle(&in_play, &new_game, &started);
    Message stray = {};
    stray.envelope = Envelope_Event(MSG_REPLY, kAwaitingRules, kInPlay);
    EXPECT_TRUE(Exchange(&awaiting, &in_play, {stray}, 100).empty());
}

TEST(ProtocolTest, should_have_a_game_awaiting_rules_answer_no_answer_with_no_game)
{
    for (const Selector answer :
         {MSG_REPLY, MSG_FRAME_CHANGED, MSG_ROLL_HELD, MSG_ROLLS_LOST, MSG_NOT_UNDERSTOOD, MSG_STATS}) {
        GameActor game;
        GameActor_Init(&game, kAwaitingRules);
        Message message = {};
        message.envelope = Envelope_Event(answer, kAsker, kAwaitingRules);
        TestOutbox outbox;
        GameActor_Handle(&game, &message, &outbox);
        EXPECT_EQ(0U, outbox.count) << "selector " << answer;
    }
}

TEST(ProtocolTest, should_class_every_selector_as_a_request_or_an_answer)
{
    const std::vector<Selector> answers = {MSG_REPLY,          MSG_FRAME_CHANGED, MSG_ROLL_HELD,
                                           MSG_ROLLS_LOST,     MSG_NOT_UNDERSTOOD, MSG_STATS};
    for (int value = 0; value < static_cast<int>(MSG_SELECTOR_COUNT); value++) {
        const Selector selector = static_cast<Selector>(value);
        const bool is_an_answer =
            std::find(answers.begin(), answers.end(), selector) != answers.end();
        EXPECT_EQ(!is_an_answer, Selector_IsARequest(selector)) << "selector " << value;
    }
}

TEST(ProtocolTest, should_class_a_selector_past_the_protocols_end_as_a_request)
{
    /* A sender asked for something no kind knows: its answer is NOT_UNDERSTOOD. */
    for (const int past : {static_cast<int>(MSG_SELECTOR_COUNT), 200}) {
        EXPECT_TRUE(Selector_IsARequest(static_cast<Selector>(past))) << "selector " << past;
    }
}

TEST(ProtocolTest, should_want_an_answer_only_for_a_request_from_someone)
{
    const Envelope request = Envelope_Event(MSG_ROLL, kAsker, kKind);
    const Envelope from_no_one = Envelope_Event(MSG_ROLL, ACTOR_ID_NONE, kKind);
    const Envelope answer = Envelope_Event(MSG_REPLY, kAsker, kKind);
    EXPECT_TRUE(Envelope_WantsAnAnswer(&request));
    EXPECT_FALSE(Envelope_WantsAnAnswer(&from_no_one));
    EXPECT_FALSE(Envelope_WantsAnAnswer(&answer));
}

TEST(ProtocolTest, should_address_a_not_understood_to_the_request_it_answers)
{
    /* Every kind answers through the outbox, so its addressing is checked here, once. */
    Message request = {};
    request.envelope = Envelope_Event(MSG_ROLL, kAsker, kKind);
    request.envelope.seq = 5U;
    TestOutbox outbox;
    Outbox_NotUnderstood(&outbox, &request);
    ASSERT_EQ(1U, outbox.count);
    const Message &answer = outbox.items[0];
    EXPECT_EQ(MSG_NOT_UNDERSTOOD, answer.envelope.selector);
    EXPECT_EQ(kKind, answer.envelope.from);
    EXPECT_EQ(kAsker, answer.envelope.to);
    EXPECT_EQ(5U, answer.envelope.seq);
    EXPECT_EQ(MSG_ROLL, answer.payload.not_understood.selector);
}
