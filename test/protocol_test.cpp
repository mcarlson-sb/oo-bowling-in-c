/* What the protocol promises of every kind, whatever sits at an id: here, that a client can always
 * ask for the facts, with QUERY_STATS, without knowing which kind it asks. */

#include <gtest/gtest.h>

#include "test_outbox.h"

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
    Scoreboard_Init(&board, kKind);
    const Message to_board = StatsQuery(2U);
    Scoreboard_Handle(&board, &to_board, &to_the_board);
    ExpectStatsIn(to_the_board, 2U, "a scoreboard");
    TestOutbox to_the_average;
    RunningAverage average;
    RunningAverage_Init(&average, kKind);
    const Message to_average = StatsQuery(3U);
    RunningAverage_Handle(&average, &to_average, &to_the_average);
    ExpectStatsIn(to_the_average, 3U, "a running average");
}
